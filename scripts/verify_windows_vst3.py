#!/usr/bin/env python3
"""Fail-closed Windows x64 Skell Delay VST3 staging and provenance gate."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path
from typing import Any

PRODUCT = "Skell-Delay"
VENDOR = "Skelly Audio"
MODULE = Path("Contents/x86_64-win/Skell-Delay.vst3")
INFO = Path("Contents/Resources/moduleinfo.json")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")
SHA_RE = re.compile(r"^[0-9a-f]{40}$")


class PackageError(ValueError):
    pass


def strip_trailing_commas(source: str) -> str:
    """Remove only commas before JSON closers outside strings."""
    out: list[str] = []
    quoted = escaped = False
    i = 0
    while i < len(source):
        ch = source[i]
        if quoted:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                quoted = False
            i += 1
            continue
        if ch == '"':
            quoted = True
            out.append(ch)
        elif ch == ",":
            j = i + 1
            while j < len(source) and source[j].isspace():
                j += 1
            if j < len(source) and source[j] in "}]":
                i += 1
                continue
            out.append(ch)
        else:
            out.append(ch)
        i += 1
    if quoted:
        raise PackageError("moduleinfo.json ends inside an unterminated string")
    return "".join(out)


def unique_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"Duplicate moduleinfo JSON key: {key!r}")
        result[key] = value
    return result


def reject_non_finite_constant(token: str) -> Any:
    """Reject Python json's non-standard NaN/Infinity extensions."""
    raise PackageError(f"Non-finite JSON numeric constant is forbidden: {token}")


def identity_from_cmake(path: Path) -> dict[str, str]:
    text = path.read_text(encoding="utf-8-sig")
    project = re.search(r"(?m)^\s*project\s*\(\s*DelayPlugin\s+VERSION\s+([^\s)]+)", text)
    vendor = re.search(r'(?m)^\s*COMPANY_NAME\s+"([^"]+)"', text)
    product = re.search(r'(?m)^\s*PRODUCT_NAME\s+"([^"]+)"', text)
    plugin = re.search(r"(?is)juce_add_plugin\s*\(\s*DelayPlugin\b(.*?)\)", text)
    formats = re.search(r"(?im)^\s*FORMATS\s+([^\r\n)]+)", plugin.group(1)) if plugin else None
    if not (project and vendor and product and plugin and formats):
        raise PackageError("CMakeLists.txt is missing DelayPlugin VERSION, COMPANY_NAME, PRODUCT_NAME or FORMATS")
    if formats.group(1).split() != ["VST3"]:
        raise PackageError("Skell Delay release target must declare FORMATS VST3 only (no AU or Standalone targets)")
    version = project.group(1)
    if not VERSION_RE.fullmatch(version):
        raise PackageError(f"Invalid CMake semantic version: {version!r}")
    identity = {"product": product.group(1), "vendor": vendor.group(1), "version": version}
    if identity["product"] != PRODUCT or identity["vendor"] != VENDOR:
        raise PackageError("CMake product/vendor changed; stop and review release identity compatibility")
    return identity


def read_metadata(path: Path) -> tuple[dict[str, Any], str, bool]:
    original = path.read_text(encoding="utf-8-sig")
    normalized = strip_trailing_commas(original)
    try:
        data = json.loads(normalized, object_pairs_hook=unique_keys,
                          parse_constant=reject_non_finite_constant)
    except (json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"moduleinfo.json invalid after safe JUCE normalization: {exc}") from exc
    if not isinstance(data, dict):
        raise PackageError("moduleinfo.json root must be an object")
    changed = normalized != original
    if changed:
        path.write_text(normalized, encoding="utf-8", newline="\n")
    return data, normalized, changed


def validate_metadata(data: dict[str, Any], expected: dict[str, str]) -> list[str]:
    if data.get("Name") != expected["product"]:
        raise PackageError(f"Product mismatch: {data.get('Name')!r}")
    if data.get("Version") != expected["version"]:
        raise PackageError(f"Version mismatch: expected {expected['version']!r}, got {data.get('Version')!r}")
    factory = data.get("Factory Info")
    if not isinstance(factory, dict) or factory.get("Vendor") != expected["vendor"]:
        raise PackageError("Factory Info Vendor does not match CMake COMPANY_NAME")
    classes = data.get("Classes")
    if not isinstance(classes, list) or len(classes) != 2:
        raise PackageError("Exactly two VST3 classes (component and controller) are required")
    categories: list[str] = []
    cids: list[str] = []
    for index, item in enumerate(classes):
        if not isinstance(item, dict):
            raise PackageError(f"VST3 class {index} is not an object")
        if (item.get("Name"), item.get("Vendor"), item.get("Version")) != (
            expected["product"], expected["vendor"], expected["version"]
        ):
            raise PackageError(f"VST3 class {index} identity mismatch")
        category, cid = item.get("Category"), item.get("CID")
        if not isinstance(category, str):
            raise PackageError(f"VST3 class {index} has no Category")
        if not isinstance(cid, str) or not CID_RE.fullmatch(cid):
            raise PackageError(f"VST3 class {index} has an invalid 128-bit CID")
        categories.append(category)
        cids.append(cid.lower())
    if sorted(categories) != ["Audio Module Class", "Component Controller Class"]:
        raise PackageError("Component and controller categories must each appear exactly once")
    if len(set(cids)) != 2:
        raise PackageError("VST3 component/controller CIDs must be unique")
    return cids


def validate_pe32_plus_x64(binary: bytes) -> None:
    if len(binary) < 0x40 or binary[:2] != b"MZ":
        raise PackageError("Windows PE MZ signature missing or truncated")
    offset = struct.unpack_from("<I", binary, 0x3C)[0]
    if offset < 0x40 or offset + 26 > len(binary) or binary[offset:offset + 4] != b"PE\0\0":
        raise PackageError("Invalid or out-of-range Windows PE signature")
    machine, sections = struct.unpack_from("<HH", binary, offset + 4)
    optional_size, characteristics = struct.unpack_from("<HH", binary, offset + 20)
    magic = struct.unpack_from("<H", binary, offset + 24)[0]
    if machine != 0x8664:
        raise PackageError(f"Expected AMD64 PE machine 0x8664, got 0x{machine:04x}")
    if magic != 0x20B:
        raise PackageError(f"Expected PE32+ optional header 0x20b, got 0x{magic:03x}")
    if sections == 0 or optional_size < 0x70 or offset + 24 + optional_size > len(binary):
        raise PackageError("PE section table or PE32+ optional header is invalid")
    if not characteristics & 0x2000:
        raise PackageError("VST3 module is not marked as a Windows DLL")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def check_bundle(bundle: Path) -> None:
    if not bundle.is_dir() or bundle.name != PRODUCT + ".vst3":
        raise PackageError(f"Expected {PRODUCT}.vst3 directory")
    modules = []
    for path in bundle.rglob("*"):
        if path.is_symlink():
            raise PackageError(f"Symlink in bundle: {path.relative_to(bundle)}")
        if path.is_file():
            if path.suffix.lower() in {".exe", ".zip", ".component"}:
                raise PackageError(f"Unexpected standalone/AU/archive file: {path.name}")
            if path.suffix.lower() == ".vst3":
                modules.append(path)
    if modules != [bundle / MODULE]:
        raise PackageError(f"Exactly one module is required at {MODULE.as_posix()}")
    if not (bundle / INFO).is_file():
        raise PackageError(f"Missing {INFO.as_posix()}")


def stage(source: Path, package: Path, cmake: Path, source_sha: str,
          ci_sha: str, run_id: str, pluginval: int) -> dict[str, Any]:
    expected = identity_from_cmake(cmake)
    if not SHA_RE.fullmatch(source_sha) or not SHA_RE.fullmatch(ci_sha):
        raise PackageError("Source and CI checkout commits must be full 40-character Git SHAs")
    if not run_id.isdigit():
        raise PackageError("GitHub Actions run ID must be numeric")
    if pluginval != 10:
        raise PackageError("Release provenance requires pluginval strictness 10")

    source = source.resolve()
    check_bundle(source)
    siblings = sorted(source.parent.glob("*.vst3"))
    if siblings != [source]:
        raise PackageError(f"Expected one VST3 bundle in build output, found {len(siblings)}")
    if package.exists() and any(package.iterdir()):
        raise PackageError(f"Refusing to reuse non-empty staging directory: {package}")
    package.mkdir(parents=True, exist_ok=True)
    staged = package / source.name
    shutil.copytree(source, staged, symlinks=False)
    check_bundle(staged)

    info_path = staged / INFO
    metadata, normalized, changed = read_metadata(info_path)
    cids = validate_metadata(metadata, expected)
    binary_path = staged / MODULE
    binary = binary_path.read_bytes()
    validate_pe32_plus_x64(binary)

    manifest = {
        "schema_version": 1,
        "product": expected["product"],
        "vendor": expected["vendor"],
        "version": expected["version"],
        "format": "VST3",
        "platform": "windows-x64",
        "source_commit": source_sha,
        "ci_checkout_commit": ci_sha,
        "workflow_run_id": run_id,
        "binary": {
            "path": f"{staged.name}/{MODULE.as_posix()}",
            "size_bytes": len(binary),
            "sha256": sha(binary),
            "architecture": "AMD64",
            "pe_format": "PE32+",
        },
        "moduleinfo": {
            "path": f"{staged.name}/{INFO.as_posix()}",
            "sha256": sha(normalized.encode("utf-8")),
            "strict_json": True,
            "unique_class_ids": cids,
            "normalization_applied": changed,
        },
        "validation": {
            "bundle_structure": "passed",
            "metadata_identity": "passed",
            "pe32_plus_amd64": "passed",
            "pluginval": {"strictness_level": pluginval, "result": "passed"},
        },
        "release_status": "preview-only",
        "release_blockers": [
            "FL Studio scan, automation, state save/reopen and old-project compatibility are not accepted",
            "Real music A/B and owner listening approval are pending",
            "Factory Info URL and E-Mail are empty in current generated metadata",
            "Target Windows hardware CPU/latency and Quick4 UI acceptance are pending",
            "Third-party license/attribution and SkellHub updater/rollback review are pending",
        ],
    }
    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    (package / "build-info.json").write_text(manifest_text, encoding="utf-8", newline="\n")
    sums = [
        f"{manifest['binary']['sha256']}  {manifest['binary']['path']}",
        f"{manifest['moduleinfo']['sha256']}  {manifest['moduleinfo']['path']}",
        f"{sha(manifest_text.encode('utf-8'))}  build-info.json",
    ]
    (package / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n", encoding="ascii", newline="\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-bundle", type=Path, required=True)
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, default=Path("CMakeLists.txt"))
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--ci-commit", required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--pluginval-strictness", type=int, required=True)
    args = parser.parse_args()
    try:
        result = stage(args.source_bundle, args.package_dir, args.cmake,
                       args.source_commit, args.ci_commit, args.run_id,
                       args.pluginval_strictness)
        print(f"PASS: {result['product']} {result['version']} Windows x64 VST3; "
              f"normalization={result['moduleinfo']['normalization_applied']}; "
              f"binary SHA256={result['binary']['sha256']}")
        print("PASS: one bundle/module, unique CIDs, strict identity, PE32+ AMD64 DLL, pluginval 10, provenance")
        return 0
    except (PackageError, OSError, UnicodeError, ValueError) as exc:
        print(f"RELEASE GATE FAILED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
