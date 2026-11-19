#!/usr/bin/env python3
"""Fail-closed Windows x64 Skell Delay VST3 staging and provenance gate."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from datetime import date
import shutil
import struct
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
PREVIEW_INSTALLER = REPO_ROOT / "installer" / "windows" / "Install-SkellDelayPreview.ps1"
PREVIEW_GUIDE = REPO_ROOT / "docs" / "INSTALL_PREVIEW.md"
LEGAL_APPROVAL = REPO_ROOT / "legal" / "RELEASE_APPROVAL.json"
THIRD_PARTY_NOTICES = REPO_ROOT / "legal" / "THIRD_PARTY_NOTICES.txt"
EULA = REPO_ROOT / "legal" / "EULA.txt"
VST3_SDK_LICENSE = Path("modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt")

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


def load_release_approval(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise PackageError(f"Missing owner release approval record: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"),
                          object_pairs_hook=unique_keys,
                          parse_constant=reject_non_finite_constant)
    except (json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"Release approval record is not strict JSON: {exc}") from exc
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise PackageError("Release approval record must be a schema_version 1 JSON object")
    return data


def _required_approval_text(section: dict[str, Any], key: str, label: str) -> str:
    value = section.get(key)
    if not isinstance(value, str) or not value.strip() or value != value.strip():
        raise PackageError(f"Owner legal approval is missing {label}")
    if value.upper() in {"UNRESOLVED", "NOT_PROVIDED", "NOT APPROVED",
                         "NOT_REVIEWED", "UNDECIDED", "TBD", "N/A", "NONE"}:
        raise PackageError(f"Owner legal approval is unresolved: {label}")
    return value


def validate_release_legal_approval(approval_path: Path = LEGAL_APPROVAL,
                                    eula_path: Path = EULA) -> dict[str, Any]:
    data = load_release_approval(approval_path)
    if data.get("status") != "APPROVED":
        raise PackageError(f"Release legal approval is not APPROVED (status={data.get('status')!r})")
    _required_approval_text(data, "approver", "approver")
    approved_at = _required_approval_text(data, "approved_at", "approval date")
    try:
        date.fromisoformat(approved_at)
    except ValueError as exc:
        raise PackageError("Owner legal approval date must use ISO YYYY-MM-DD") from exc
    _required_approval_text(data, "approval_reference", "approval reference")

    juce = data.get("juce")
    if not isinstance(juce, dict) or juce.get("license_mode") not in {"COMMERCIAL", "AGPLv3"}:
        raise PackageError("JUCE license_mode must be explicitly COMMERCIAL or AGPLv3")
    _required_approval_text(juce, "license_evidence_reference", "JUCE license evidence")
    if juce["license_mode"] == "COMMERCIAL":
        _required_approval_text(juce, "commercial_tier", "JUCE commercial tier")
    else:
        if juce.get("commercial_tier") != "NOT_APPLICABLE":
            raise PackageError("AGPLv3 selection must set commercial_tier to NOT_APPLICABLE")
        _required_approval_text(juce, "source_distribution_reference", "AGPLv3 source distribution reference")

    vst3 = data.get("vst3_sdk")
    if not isinstance(vst3, dict) or vst3.get("terms_reviewed") is not True:
        raise PackageError("Steinberg VST3 SDK terms have not been owner-reviewed")
    _required_approval_text(vst3, "license_reference", "VST3 SDK license reference")
    inventory = data.get("third_party_inventory")
    if not isinstance(inventory, dict) or inventory.get("reviewed") is not True:
        raise PackageError("The compiled third-party dependency inventory has not been reviewed")
    _required_approval_text(inventory, "evidence_reference", "third-party inventory evidence")
    assets = data.get("assets")
    if not isinstance(assets, dict) or assets.get("commercial_redistribution_approved") is not True:
        raise PackageError("Commercial redistribution rights for bundled artwork/assets are not approved")
    _required_approval_text(assets, "evidence_reference", "asset-rights evidence")
    eula = data.get("eula")
    if not isinstance(eula, dict) or eula.get("reviewed") is not True:
        raise PackageError("The customer EULA has not been reviewed")
    _required_approval_text(eula, "evidence_reference", "EULA review evidence")
    if not eula_path.is_file():
        raise PackageError(f"Missing reviewed customer EULA: {eula_path}")
    eula_text = eula_path.read_text(encoding="utf-8-sig")
    if re.search(r"(?i)\bTEMPLATE\b|\[[^\]]+\]", eula_text):
        raise PackageError("EULA still contains a template marker or unresolved bracketed placeholder")
    trademark = data.get("trademark")
    if not isinstance(trademark, dict) or trademark.get("reviewed") is not True:
        raise PackageError("Product/publisher trademark clearance has not been reviewed")
    _required_approval_text(trademark, "evidence_reference", "trademark review evidence")
    return data


def render_third_party_notices(template_path: Path, approval: dict[str, Any] | None) -> str:
    if not template_path.is_file():
        raise PackageError(f"Missing third-party notice template: {template_path}")
    text = template_path.read_text(encoding="utf-8-sig")
    if approval is None:
        mode, tier = "UNRESOLVED — OWNER APPROVAL REQUIRED", "NOT APPROVED"
        status = "PRIVATE PREVIEW ONLY — NOT FOR REDISTRIBUTION OR SALE"
    else:
        juce = approval["juce"]
        mode = juce["license_mode"]
        tier = juce["commercial_tier"] if mode == "COMMERCIAL" else "NOT APPLICABLE"
        status = "SIGNED RELEASE CANDIDATE — CI DOES NOT PUBLISH A PUBLIC RELEASE"
    for token, value in (("{{JUCE_LICENSE_MODE}}", mode),
                         ("{{JUCE_COMMERCIAL_TIER}}", tier),
                         ("{{DISTRIBUTION_STATUS}}", status)):
        text = text.replace(token, value)
    if "{{" in text or "}}" in text:
        raise PackageError("Third-party notice template contains an unresolved token")
    return text


def stage_juce_licenses(juce_source: Path, package: Path) -> list[dict[str, Any]]:
    if not juce_source.is_dir():
        raise PackageError(f"Missing populated JUCE FetchContent source directory: {juce_source}")
    sources = (
        (juce_source / "LICENSE.md", Path("ThirdPartyLicenses/JUCE-LICENSE.md")),
        (juce_source / VST3_SDK_LICENSE, Path("ThirdPartyLicenses/VST3-SDK-LICENSE.txt")),
    )
    records: list[dict[str, Any]] = []
    for source, relative in sources:
        if not source.is_file() or source.stat().st_size < 200:
            raise PackageError(f"Missing or implausibly short upstream license text: {source}")
        destination = package / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        records.append({"path": relative.as_posix(), "size_bytes": destination.stat().st_size,
                        "sha256": sha(destination.read_bytes())})
    return records



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
          ci_sha: str, run_id: str, pluginval: int,
          public_release_prepared: bool = False,
          juce_source: Path | None = None,
          legal_approval: Path = LEGAL_APPROVAL,
          eula_path: Path = EULA,
          notices_path: Path = THIRD_PARTY_NOTICES) -> dict[str, Any]:
    expected = identity_from_cmake(cmake)
    approval_record = load_release_approval(legal_approval)
    approved_legal = validate_release_legal_approval(legal_approval, eula_path) if public_release_prepared else None
    if juce_source is None:
        raise PackageError("The populated JUCE source directory is required to package upstream license texts")
    if not (juce_source / "LICENSE.md").is_file() or not (juce_source / VST3_SDK_LICENSE).is_file():
        raise PackageError("Pinned JUCE/VST3 SDK license texts are missing from the dependency source")
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

    for source_file, package_name in (
        (PREVIEW_INSTALLER, "Install-SkellDelayPreview.ps1"),
        (PREVIEW_GUIDE, "INSTALL_PREVIEW.md"),
    ):
        if not source_file.is_file():
            raise PackageError(f"Missing private-preview distribution file: {source_file}")
        shutil.copy2(source_file, package / package_name)

    notice_path = package / "THIRD_PARTY_NOTICES.txt"
    notice_path.write_text(render_third_party_notices(notices_path, approved_legal), encoding="utf-8", newline="\n")
    license_records = stage_juce_licenses(juce_source, package)
    eula_record = None
    if public_release_prepared:
        shutil.copy2(eula_path, package / "EULA.txt")
        eula_record = {"path": "EULA.txt", "sha256": sha((package / "EULA.txt").read_bytes())}

    info_path = staged / INFO
    metadata, normalized, changed = read_metadata(info_path)
    cids = validate_metadata(metadata, expected)
    binary_path = staged / MODULE
    binary = binary_path.read_bytes()
    validate_pe32_plus_x64(binary)

    bundle_files = []
    for path in sorted((p for p in staged.rglob("*") if p.is_file()), key=lambda p: p.relative_to(package).as_posix()):
        relative = f"{staged.name}/{path.relative_to(staged).as_posix()}"
        data = path.read_bytes()
        bundle_files.append({"path": relative, "size_bytes": len(data), "sha256": sha(data)})
    installer_path = package / "Install-SkellDelayPreview.ps1"
    guide_path = package / "INSTALL_PREVIEW.md"
    installer_record = {"path": installer_path.name, "sha256": sha(installer_path.read_bytes())}
    guide_record = {"path": guide_path.name, "sha256": sha(guide_path.read_bytes())}

    notice_record = {"path": notice_path.name, "sha256": sha(notice_path.read_bytes())}
    legal_approval_record = {
        "status": "owner-approved" if public_release_prepared else "not-approved-for-distribution",
        "approval_record_sha256": sha(legal_approval.read_bytes()) if public_release_prepared else None,
        "third_party_notices": notice_record,
        "upstream_license_texts": license_records,
        "eula": eula_record,
    }

    factory_info = metadata.get("Factory Info")
    factory_url = factory_info.get("URL") if isinstance(factory_info, dict) else ""
    support_email = factory_info.get("E-Mail") if isinstance(factory_info, dict) else ""
    factory_url_present = isinstance(factory_url, str) and bool(factory_url.strip())
    support_email_present = isinstance(support_email, str) and bool(support_email.strip())
    publisher_contact_metadata_present = factory_url_present and support_email_present
    if public_release_prepared and not publisher_contact_metadata_present:
        raise PackageError("Public release preparation was asserted, but Factory Info URL/E-Mail are missing")
    publisher_contact_complete = public_release_prepared and publisher_contact_metadata_present
    release_blockers = [
        "FL Studio scan, automation, state save/reopen and old-project compatibility are not accepted",
        "Real music A/B and owner listening approval are pending",
        "Target Windows hardware CPU/latency and Quick4 UI acceptance are pending",
        "SkellHub updater/rollback review is pending",
    ]
    if not publisher_contact_complete:
        release_blockers.append(
            "Owner-approved Factory Info URL and E-Mail are required before a public version-tag build"
        )
    if not public_release_prepared:
        release_blockers.append(
            "Owner-approved JUCE/VST3 licensing, full dependency inventory, artwork rights, trademark clearance and reviewed EULA are required before public distribution"
        )

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
        "bundle_files": bundle_files,
        "installer": {
            "scope": "signed release candidate" if public_release_prepared else "per-user private preview",
            "script": installer_record,
            "guide": guide_record,
        },
        "legal": legal_approval_record,
        "validation": {
            "bundle_structure": "passed",
            "metadata_identity": "passed",
            "pe32_plus_amd64": "passed",
            "pluginval": {"strictness_level": pluginval, "result": "passed"},
            "third_party_notice_packaging": "passed",
            "upstream_license_texts": "included",
            "owner_legal_approval": "passed" if public_release_prepared else "not-approved-preview-only",
        },
        "publisher_contact": {
            "complete": publisher_contact_complete,
            "metadata_present": publisher_contact_metadata_present,
            "validated_by_tag_gate": public_release_prepared,
            "factory_info_url_present": factory_url_present,
            "support_email_present": support_email_present,
            "source": "owner-approved tag preparation step" if publisher_contact_complete else "not validated for public release",
        },
        "release_status": "signed-release-candidate" if public_release_prepared else "preview-only",
        "release_blockers": release_blockers,
    }
    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    (package / "build-info.json").write_text(manifest_text, encoding="utf-8", newline="\n")
    sums = [f"{item['sha256']}  {item['path']}" for item in bundle_files]
    sums.extend([
        f"{installer_record['sha256']}  {installer_record['path']}",
        f"{guide_record['sha256']}  {guide_record['path']}",
        f"{notice_record['sha256']}  {notice_record['path']}",
        *[f"{item['sha256']}  {item['path']}" for item in license_records],
        *([f"{eula_record['sha256']}  {eula_record['path']}"] if eula_record else []),
        f"{sha(manifest_text.encode('utf-8'))}  build-info.json",
    ])
    sums.sort(key=lambda item: item.split("  ", 1)[1])
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
    parser.add_argument("--juce-source", type=Path, required=True)
    parser.add_argument("--legal-approval", type=Path, default=LEGAL_APPROVAL)
    parser.add_argument("--eula", type=Path, default=EULA)
    parser.add_argument("--notices", type=Path, default=THIRD_PARTY_NOTICES)
    parser.add_argument("--public-release-prepared", action="store_true",
                        help="Assert that the owner-approved version-tag metadata/signing gate passed")
    args = parser.parse_args()
    try:
        result = stage(args.source_bundle, args.package_dir, args.cmake,
                       args.source_commit, args.ci_commit, args.run_id,
                       args.pluginval_strictness, args.public_release_prepared,
                       args.juce_source, args.legal_approval, args.eula, args.notices)
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
