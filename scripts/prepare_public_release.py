#!/usr/bin/env python3
"""Apply owner-approved publisher contact metadata before a signed version-tag build."""
from __future__ import annotations

import argparse
import base64
import binascii
import hashlib
import ipaddress
import json
import os
import re
import sys
import tempfile
from datetime import date
import unicodedata
from pathlib import Path
from urllib.parse import urlsplit

from verify_windows_vst3 import (
    PackageError,
    identity_from_cmake,
    reject_non_finite_constant,
    strip_trailing_commas,
    unique_keys,
    validate_metadata,
    LEGAL_APPROVAL,
    EULA,
    validate_release_legal_approval,
)

TAG_RE = re.compile(r"^v(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?)$")
EMAIL_RE = re.compile(
    r"^[A-Za-z0-9.!#$%&'*+/=?^_{}|~-]+@"
    r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?"
    r"(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$"
)
RESERVED_HOST_SUFFIXES = (".local", ".localhost", ".invalid", ".test", ".example")
RESERVED_HOSTS = {"example.com", "example.org", "example.net"}


def _has_disallowed_unicode(value: str) -> bool:
    """Reject invisible/control code points in publisher metadata.

    ASCII spaces are allowed in a vendor name, but other whitespace, Unicode
    format controls (including bidi overrides/zero-width characters), control
    characters, surrogates, private-use and unassigned code points are not.
    """
    for char in value:
        category = unicodedata.category(char)
        if category in {"Cc", "Cf", "Cs", "Co", "Cn", "Zl", "Zp"}:
            return True
        if char != " " and char.isspace():
            return True
    return False


OWNER_ACCEPTANCE = Path(__file__).resolve().parents[1] / "release" / "OWNER_ACCEPTANCE.json"
REQUIRED_OWNER_GATES = (
    "build_download",
    "sound_a_b",
    "host_compatibility",
    "preset_state_recall",
    "licensing_attribution",
    "updater_rollback",
    "storefront_email_signup",
    "release_notes",
    "support_contact",
    "pricing_trial",
)
PLACEHOLDER_VALUES = {"", "NOT_PROVIDED", "NOT_VERIFIED", "NOT_APPROVED", "PENDING", "TBD"}


def validate_contact_url(value: str) -> str:
    if not value or value != value.strip():
        raise PackageError("SKELL_FACTORY_INFO_URL must be non-empty and have no surrounding whitespace")
    if _has_disallowed_unicode(value) or any(ch.isspace() for ch in value):
        raise PackageError("Factory Info URL must not contain whitespace or invisible/control characters")
    try:
        parsed = urlsplit(value)
        host = parsed.hostname
        _ = parsed.port
    except ValueError as exc:
        raise PackageError(f"Factory Info URL is invalid: {exc}") from exc
    if (
        parsed.scheme.lower() != "https"
        or not host
        or "." not in host
        or host.lower() in RESERVED_HOSTS
        or host.lower().endswith(RESERVED_HOST_SUFFIXES)
        or parsed.username is not None
        or parsed.password is not None
        or parsed.fragment
    ):
        raise PackageError(
            "Factory Info URL must be a public absolute HTTPS URL without credentials or fragment"
        )
    try:
        ipaddress.ip_address(host)
    except ValueError:
        pass
    else:
        raise PackageError("Factory Info URL must use a public DNS hostname, not an IP address")
    return value


def validate_support_email(value: str) -> str:
    if not value or value != value.strip():
        raise PackageError("SKELL_SUPPORT_EMAIL must be non-empty and have no surrounding whitespace")
    if _has_disallowed_unicode(value) or any(ch.isspace() for ch in value):
        raise PackageError("SKELL_SUPPORT_EMAIL must not contain whitespace or invisible/control characters")
    if not EMAIL_RE.fullmatch(value):
        raise PackageError("SKELL_SUPPORT_EMAIL must be a valid support email address")
    local, domain = value.rsplit("@", 1)
    if (
        local.startswith(".")
        or local.endswith(".")
        or ".." in local
        or domain.lower() in RESERVED_HOSTS
        or domain.lower().endswith(RESERVED_HOST_SUFFIXES)
    ):
        raise PackageError("SKELL_SUPPORT_EMAIL must use a public email domain")
    try:
        ipaddress.ip_address(domain)
    except ValueError:
        pass
    else:
        raise PackageError("SKELL_SUPPORT_EMAIL must use a public DNS domain, not an IP address")
    return value


def validate_vendor_name(value: str) -> str:
    """Require an explicit owner-approved publisher name without normalization."""
    if not value or value != value.strip() or len(value) > 80:
        raise PackageError(
            "SKELL_VENDOR_NAME must be an owner-approved name of 1-80 characters "
            "with no surrounding whitespace"
        )
    if _has_disallowed_unicode(value):
        raise PackageError(
            "SKELL_VENDOR_NAME must not contain invisible, control, private-use "
            "or unassigned characters"
        )
    return value


def validate_owner_acceptance(path: Path) -> dict:
    """Require explicit owner evidence for every commercial release-readiness gate."""
    raw = path.read_bytes()
    try:
        record = json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique_keys, parse_constant=reject_non_finite_constant)
    except (UnicodeError, json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"Owner release acceptance record is invalid: {exc}") from exc

    if not isinstance(record, dict) or type(record.get("schema_version")) is not int or record["schema_version"] != 1:
        raise PackageError("Owner release acceptance record must use schema_version 1")
    if record.get("status") != "APPROVED":
        raise PackageError("Owner release acceptance record is not APPROVED")

    def require_text(value: object, label: str, limit: int = 1024) -> str:
        if (not isinstance(value, str) or not value or value != value.strip() or len(value) > limit
                or value.upper() in PLACEHOLDER_VALUES or _has_disallowed_unicode(value)):
            raise PackageError(f"Owner release acceptance {label} is missing or contains a placeholder/invalid value")
        return value

    require_text(record.get("approver"), "approver", 120)
    approval_date = require_text(record.get("approved_at"), "approved_at", 10)
    if not re.fullmatch(r"\\d{4}-\\d{2}-\\d{2}", approval_date):
        raise PackageError("Owner release acceptance approved_at must use YYYY-MM-DD")
    try:
        date.fromisoformat(approval_date)
    except ValueError as exc:
        raise PackageError("Owner release acceptance approved_at is not a valid calendar date") from exc
    require_text(record.get("approval_reference"), "approval_reference")

    gates = record.get("gates")
    if not isinstance(gates, dict):
        raise PackageError("Owner release acceptance gates must be an object")
    missing = [name for name in REQUIRED_OWNER_GATES if name not in gates]
    if missing:
        raise PackageError("Owner release acceptance is missing required gates: " + ", ".join(missing))

    for name in REQUIRED_OWNER_GATES:
        gate = gates[name]
        if not isinstance(gate, dict):
            raise PackageError(f"Owner release acceptance gate {name} must be an object")
        if gate.get("status") not in {"PASS", "APPROVED"}:
            raise PackageError(f"Owner release acceptance gate {name} is not PASS/APPROVED")
        require_text(gate.get("evidence_reference"), f"{name}.evidence_reference")
        if name == "pricing_trial":
            require_text(gate.get("decision"), "pricing_trial.decision", 160)

    return {"status": "APPROVED", "sha256": hashlib.sha256(raw).hexdigest()}


def prepare_release(
    moduleinfo: Path,
    cmake: Path,
    tag: str,
    factory_url: str,
    support_email: str,
    vendor_name: str,
    pfx_b64: str,
    pfx_password: str,
    legal_approval: Path = LEGAL_APPROVAL,
    eula_path: Path = EULA,
    owner_acceptance_path: Path = OWNER_ACCEPTANCE,
) -> dict:
    approval = validate_release_legal_approval(legal_approval, eula_path)
    owner_acceptance = validate_owner_acceptance(owner_acceptance_path)
    match = TAG_RE.fullmatch(tag)
    if not match:
        raise PackageError("Version tag must use vMAJOR.MINOR.PATCH (optional prerelease/build suffix)")
    expected = identity_from_cmake(cmake)
    approved_vendor = validate_vendor_name(vendor_name)
    if approved_vendor != expected["vendor"]:
        raise PackageError(
            "SKELL_VENDOR_NAME must exactly match CMake COMPANY_NAME; confirm publisher identity before release"
        )
    if match.group(1) != expected["version"]:
        raise PackageError(
            f"Version tag {tag!r} does not match CMake product version {expected['version']!r}"
        )

    url = validate_contact_url(factory_url)
    email = validate_support_email(support_email)
    if not pfx_b64 or not pfx_b64.strip():
        raise PackageError("WIN_CERT_PFX is required for a signed version-tag build")
    if not pfx_password or not pfx_password.strip():
        raise PackageError("WIN_CERT_PASSWORD is required for a signed version-tag build")
    try:
        compact_pfx = "".join(pfx_b64.split())
        pfx_bytes = base64.b64decode(compact_pfx, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise PackageError("WIN_CERT_PFX must contain valid base64-encoded PFX bytes") from exc
    if not pfx_bytes:
        raise PackageError("WIN_CERT_PFX decoded to an empty certificate")

    original = moduleinfo.read_text(encoding="utf-8-sig")
    normalized = strip_trailing_commas(original)
    try:
        data = json.loads(
            normalized,
            object_pairs_hook=unique_keys,
            parse_constant=reject_non_finite_constant,
        )
    except (json.JSONDecodeError, PackageError) as exc:
        raise PackageError(f"VST3 moduleinfo.json is invalid: {exc}") from exc
    if not isinstance(data, dict):
        raise PackageError("VST3 moduleinfo.json root must be an object")
    validate_metadata(data, expected)
    factory = data.get("Factory Info")
    if not isinstance(factory, dict):
        raise PackageError("VST3 moduleinfo.json Factory Info must be an object")

    approved = {"URL": url, "E-Mail": email}
    for key, value in approved.items():
        current = factory.get(key, "")
        if current not in ("", value):
            raise PackageError(
                f"Existing Factory Info {key} differs from the owner-approved repository variable; review before release"
            )
    factory.update(approved)

    output = json.dumps(data, indent=2, sort_keys=True, allow_nan=False) + "\n"
    moduleinfo.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(
        prefix=f".{moduleinfo.name}.", suffix=".tmp", dir=moduleinfo.parent
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(output)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, moduleinfo)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

    digest = hashlib.sha256(output.encode("utf-8")).hexdigest()
    return {
        "product": expected["product"],
        "vendor": approved_vendor,
        "version": expected["version"],
        "moduleinfo_sha256": digest,
        "legal_approval_status": approval["status"],
        "legal_approval_sha256": hashlib.sha256(legal_approval.read_bytes()).hexdigest(),
        "owner_acceptance_status": owner_acceptance["status"],
        "owner_acceptance_sha256": owner_acceptance["sha256"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--moduleinfo", type=Path, required=True)
    parser.add_argument("--cmake", type=Path, default=Path("CMakeLists.txt"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--legal-approval", type=Path, default=LEGAL_APPROVAL)
    parser.add_argument("--eula", type=Path, default=EULA)
    parser.add_argument("--owner-acceptance", type=Path, default=OWNER_ACCEPTANCE)
    args = parser.parse_args()
    try:
        result = prepare_release(
            args.moduleinfo,
            args.cmake,
            args.tag,
            os.environ.get("SKELL_FACTORY_INFO_URL", ""),
            os.environ.get("SKELL_SUPPORT_EMAIL", ""),
            os.environ.get("SKELL_VENDOR_NAME", ""),
            os.environ.get("WIN_CERT_PFX", ""),
            os.environ.get("WIN_CERT_PASSWORD", ""),
            args.legal_approval,
            args.eula,
            args.owner_acceptance,
        )
        print(f"PASS: {result['product']} {result['version']} release publisher metadata applied")
        print(f"PASS: owner-approved vendor {result['vendor']!r} matches CMake COMPANY_NAME and VST3 metadata")
        print(
            "PASS: owner-approved HTTPS URL and support email validated; "
            f"moduleinfo SHA256={result['moduleinfo_sha256']}"
        )
        print("PASS: version tag matches CMake and signing certificate inputs are present")
        print("PASS: owner legal approval record validated")
        print("PASS: all owner release-readiness gates approved; acceptance SHA256=" + result["owner_acceptance_sha256"])
        return 0
    except (PackageError, OSError, UnicodeError, ValueError) as exc:
        print(f"PUBLIC RELEASE GATE FAILED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
