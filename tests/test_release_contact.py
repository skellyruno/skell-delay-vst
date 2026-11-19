"""Fail-closed checks for Skell Delay's signed public-release preparation."""
import base64
import hashlib
import importlib.util
import json
import pathlib
import struct
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import verify_windows_vst3 as VERIFIER
import prepare_public_release as GATE


def metadata():
    return {
        "Name": "Skell-Delay",
        "Version": "1.0.0",
        "Factory Info": {"Vendor": "Skelly Audio", "URL": "", "E-Mail": ""},
        "Classes": [
            {
                "CID": "ABCDEF019182FAEB536B6C79446C6179",
                "Category": "Audio Module Class",
                "Name": "Skell-Delay",
                "Vendor": "Skelly Audio",
                "Version": "1.0.0",
            },
            {
                "CID": "ABCDEF011234ABCD536B6C79446C6179",
                "Category": "Component Controller Class",
                "Name": "Skell-Delay",
                "Vendor": "Skelly Audio",
                "Version": "1.0.0",
            },
        ],
    }


def pe_amd64_dll():
    data = bytearray(1024)
    data[:2] = b"MZ"
    offset = 0x80
    struct.pack_into("<I", data, 0x3C, offset)
    data[offset : offset + 4] = b"PE\0\0"
    struct.pack_into("<HH", data, offset + 4, 0x8664, 1)
    struct.pack_into("<HH", data, offset + 20, 0xF0, 0x2022)
    struct.pack_into("<H", data, offset + 24, 0x20B)
    return bytes(data)


class PublicReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.cmake = self.root / "CMakeLists.txt"
        self.cmake.write_text(
            'project(DelayPlugin VERSION 1.0.0 LANGUAGES C CXX)\n'
            'juce_add_plugin(DelayPlugin\n'
            '  COMPANY_NAME "Skelly Audio"\n'
            '  PRODUCT_NAME "Skell-Delay"\n'
            '  FORMATS VST3\n'
            ')\n',
            encoding="utf-8",
        )
        self.bundle = self.root / "build/VST3/Skell-Delay.vst3"
        self.module = self.bundle / VERIFIER.MODULE
        self.info = self.bundle / VERIFIER.INFO
        self.module.parent.mkdir(parents=True)
        self.info.parent.mkdir(parents=True)
        self.module.write_bytes(pe_amd64_dll())
        self.info.write_text(json.dumps(metadata(), indent=2), encoding="utf-8")
        self.valid = {
            "tag": "v1.0.0",
            "factory_url": "https://skellyruno.com/plugins/skell-delay",
            "support_email": "support@skellyruno.com",
            "vendor_name": "Skelly Audio",
            "pfx_b64": base64.b64encode(b"test-pfx-bytes").decode("ascii"),
            "pfx_password": "test-password",
        }

    def prepare(self, **overrides):
        values = dict(self.valid)
        values.update(overrides)
        return GATE.prepare_release(self.info, self.cmake, **values)

    def stage(self, public_release_prepared=False):
        package = self.root / "package"
        return VERIFIER.stage(
            self.bundle, package, self.cmake, "1" * 40, "2" * 40,
            "43999999999", 10, public_release_prepared
        )

    def test_applies_owner_approved_contact_to_generated_metadata(self):
        result = self.prepare()
        data = json.loads(self.info.read_text(encoding="utf-8"))
        self.assertEqual(data["Factory Info"]["URL"], self.valid["factory_url"])
        self.assertEqual(data["Factory Info"]["E-Mail"], self.valid["support_email"])
        self.assertEqual(
            result["moduleinfo_sha256"],
            hashlib.sha256(self.info.read_bytes()).hexdigest(),
        )

    def test_requires_owner_approved_vendor_name_without_mutation(self):
        before = self.info.read_bytes()
        with self.assertRaisesRegex(GATE.PackageError, "SKELL_VENDOR_NAME"):
            self.prepare(vendor_name="")
        self.assertEqual(self.info.read_bytes(), before)

    def test_vendor_name_must_match_cmake_and_metadata_without_mutation(self):
        before = self.info.read_bytes()
        with self.assertRaisesRegex(GATE.PackageError, "must exactly match CMake COMPANY_NAME"):
            self.prepare(vendor_name="Skell Audio")
        self.assertEqual(self.info.read_bytes(), before)

    def test_vendor_name_rejects_surrounding_whitespace_and_control_characters(self):
        before = self.info.read_bytes()
        for vendor in (" Skelly Audio", "Skelly Audio ", "Skelly Audio\nOther"):
            with self.subTest(vendor=vendor):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(vendor_name=vendor)
                self.assertEqual(self.info.read_bytes(), before)

    def test_vendor_name_rejects_unicode_format_controls_and_non_ascii_spaces(self):
        before = self.info.read_bytes()
        for vendor in (
            "Skelly\u202e Audio",
            "Skelly\u200b Audio",
            "Skelly\u00a0Audio",
            "Skelly\u0085Audio",
            "Skelly\u2028Audio",
        ):
            with self.subTest(vendor=vendor):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(vendor_name=vendor)
                self.assertEqual(self.info.read_bytes(), before)

    def test_factory_url_rejects_padding_and_invisible_characters(self):
        before = self.info.read_bytes()
        for url in (
            " https://skellyruno.com/plugins/skell-delay",
            "https://skellyruno.com/plugins/skell-delay ",
            "https://skelly\u200bruno.com/plugins/skell-delay",
            "https://skellyruno.com/\u202e",
        ):
            with self.subTest(url=url):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(factory_url=url)
                self.assertEqual(self.info.read_bytes(), before)

    def test_support_email_rejects_padding_and_invisible_characters(self):
        before = self.info.read_bytes()
        for email in (
            " support@skellyruno.com",
            "support@skellyruno.com ",
            "support@\u200bskellyruno.com",
            "support@skellyruno.com\u0085",
        ):
            with self.subTest(email=email):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(support_email=email)
                self.assertEqual(self.info.read_bytes(), before)

    def test_tag_must_match_cmake_version_without_mutation(self):
        before = self.info.read_bytes()
        with self.assertRaisesRegex(GATE.PackageError, "does not match CMake"):
            self.prepare(tag="v1.0.1")
        self.assertEqual(self.info.read_bytes(), before)

    def test_requires_public_https_factory_url(self):
        before = self.info.read_bytes()
        for url in (
            "http://skellyruno.com",
            "",
            "https://user:pass@skellyruno.com",
            "https://example.com",
            "https://localhost",
            "https://skellyruno.com/#fragment",
        ):
            with self.subTest(url=url):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(factory_url=url)
                self.assertEqual(self.info.read_bytes(), before)

    def test_requires_public_support_email(self):
        before = self.info.read_bytes()
        for email in (
            "",
            "support@",
            "Support Team <support@skellyruno.com>",
            "support@localhost",
            "support@example.com",
            "support@192.168.1.10",
        ):
            with self.subTest(email=email):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(support_email=email)
                self.assertEqual(self.info.read_bytes(), before)

    def test_requires_both_signing_inputs_and_valid_pfx_base64(self):
        before = self.info.read_bytes()
        for overrides in (
            {"pfx_b64": ""},
            {"pfx_password": ""},
            {"pfx_b64": "%%%"},
        ):
            with self.subTest(overrides=overrides):
                with self.assertRaises(GATE.PackageError):
                    self.prepare(**overrides)
                self.assertEqual(self.info.read_bytes(), before)

    def test_accepts_wrapped_base64_certificate_secret(self):
        wrapped = base64.encodebytes(b"test-pfx-bytes").decode("ascii")
        result = self.prepare(pfx_b64=wrapped)
        self.assertEqual(result["version"], "1.0.0")

    def test_rejects_conflicting_existing_contact_without_overwrite(self):
        data = metadata()
        data["Factory Info"]["URL"] = "https://old-owner-domain.example"
        self.info.write_text(json.dumps(data), encoding="utf-8")
        before = self.info.read_bytes()
        with self.assertRaisesRegex(GATE.PackageError, "differs from the owner-approved"):
            self.prepare()
        self.assertEqual(self.info.read_bytes(), before)

    def test_rejects_duplicate_or_non_finite_json_without_mutation(self):
        for raw in (
            '{"Name":"Skell-Delay","Name":"Other"}',
            json.dumps(metadata()).replace('"URL": ""', '"URL": NaN'),
        ):
            with self.subTest(raw=raw):
                self.info.write_text(raw, encoding="utf-8")
                before = self.info.read_bytes()
                with self.assertRaises(GATE.PackageError):
                    self.prepare()
                self.assertEqual(self.info.read_bytes(), before)

    def test_rejects_wrong_vendor_or_product_identity(self):
        for field, value in (("Vendor", "Other Audio"), ("Name", "Other")):
            data = metadata()
            if field == "Vendor":
                data["Factory Info"]["Vendor"] = value
            else:
                data["Name"] = value
            self.info.write_text(json.dumps(data), encoding="utf-8")
            before = self.info.read_bytes()
            with self.assertRaises(GATE.PackageError):
                self.prepare()
            self.assertEqual(self.info.read_bytes(), before)

    def test_preview_manifest_marks_missing_owner_contact_as_blocker(self):
        result = self.stage()
        self.assertFalse(result["publisher_contact"]["complete"])
        self.assertFalse(result["publisher_contact"]["validated_by_tag_gate"])
        self.assertIn(
            "Owner-approved Factory Info URL and E-Mail are required before a public version-tag build",
            result["release_blockers"],
        )

    def test_signed_candidate_metadata_removes_only_contact_blocker(self):
        self.prepare()
        result = self.stage(public_release_prepared=True)
        self.assertTrue(result["publisher_contact"]["complete"])
        self.assertEqual(
            result["publisher_contact"]["source"],
            "owner-approved tag preparation step",
        )
        self.assertNotIn(
            "Owner-approved Factory Info URL and E-Mail are required before a public version-tag build",
            result["release_blockers"],
        )
        manifest = json.loads((self.root / "package/build-info.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["publisher_contact"]["complete"])
        self.assertEqual(manifest["release_status"], "preview-only")

    def test_signing_script_fails_closed_and_cleans_temporary_pfx(self):
        source = (ROOT / "installer/windows/build_installer.ps1").read_text(encoding="utf-8")
        self.assertIn("LASTEXITCODE", source)
        self.assertIn("finally", source)
        self.assertIn("Remove-Item -LiteralPath $pfx -Force", source)
        self.assertIn("Expected exactly one VST3 module to sign", source)

    def test_tag_manifest_flag_cannot_be_set_without_prepared_contact(self):
        with self.assertRaisesRegex(VERIFIER.PackageError, "Factory Info URL/E-Mail are missing"):
            self.stage(public_release_prepared=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
