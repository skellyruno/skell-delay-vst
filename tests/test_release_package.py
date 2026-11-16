"""Offline regression tests for the Skell Delay Windows VST3 release gate."""
import importlib.util
import json
import pathlib
import struct
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "verify_windows_vst3.py"
SPEC = importlib.util.spec_from_file_location("verify_windows_vst3", SCRIPT)
GATE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(GATE)


def pe(machine=0x8664, magic=0x20B, signature=b"PE\0\0", sections=1, dll=True):
    data = bytearray(1024)
    data[:2] = b"MZ"
    off = 0x80
    struct.pack_into("<I", data, 0x3C, off)
    data[off:off + 4] = signature
    struct.pack_into("<HH", data, off + 4, machine, sections)
    struct.pack_into("<HH", data, off + 20, 0xF0, 0x2022 if dll else 0x0022)
    struct.pack_into("<H", data, off + 24, magic)
    return bytes(data)


def metadata(product="Skell-Delay", vendor="Skelly Audio", version="1.0.0",
             component="ABCDEF019182FAEB536B6C79446C6179",
             controller="ABCDEF011234ABCD536B6C79446C6179"):
    return {
        "Name": product, "Version": version,
        "Factory Info": {"Vendor": vendor, "URL": "", "E-Mail": "",
                         "Description": 'comma ,} inside string, quote "kept"'},
        "Classes": [
            {"CID": component, "Category": "Audio Module Class", "Name": product,
             "Vendor": vendor, "Version": version},
            {"CID": controller, "Category": "Component Controller Class", "Name": product,
             "Vendor": vendor, "Version": version},
        ],
    }


def trailing_commas(text):
    out, quoted, escaped = [], False, False
    for ch in text:
        if quoted:
            out.append(ch)
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == '"': quoted = False
        elif ch == '"':
            quoted = True
            out.append(ch)
        elif ch in "}]":
            out.extend([",", ch])
        else:
            out.append(ch)
    return "".join(out)


def render(**kwargs):
    return trailing_commas(json.dumps(metadata(**kwargs), indent=2))


class ReleaseGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.cmake = self.root / "CMakeLists.txt"
        self.cmake.write_text(
            'project(DelayPlugin VERSION 1.0.0 LANGUAGES C CXX)\n'
            'juce_add_plugin(DelayPlugin COMPANY_NAME "Skelly Audio" PRODUCT_NAME "Skell-Delay")\n',
            encoding="utf-8")
        self.bundle = self.root / "build/VST3/Skell-Delay.vst3"
        self.module = self.bundle / GATE.MODULE
        self.info = self.bundle / GATE.INFO
        self.module.parent.mkdir(parents=True)
        self.info.parent.mkdir(parents=True)
        self.module.write_bytes(pe())
        self.info.write_text(render(), encoding="utf-8")

    def run_stage(self, **kw):
        return GATE.stage(self.bundle, self.root / "package", self.cmake,
                          "1" * 40, "2" * 40, "43999999999", 10)

    def test_normalizes_only_trailing_commas_outside_strings(self):
        src = trailing_commas(json.dumps({"s": 'comma ,} and "quote"', "a": [1, 2]}))
        self.assertEqual(json.loads(GATE.strip_trailing_commas(src))["s"], 'comma ,} and "quote"')

    def test_realistic_bundle_stages_manifest_and_keeps_source_unchanged(self):
        before = self.info.read_bytes()
        result = self.run_stage()
        self.assertEqual(result["release_status"], "preview-only")
        self.assertTrue(result["moduleinfo"]["normalization_applied"])
        self.assertEqual(self.info.read_bytes(), before)
        staged_info = self.root / "package/Skell-Delay.vst3" / GATE.INFO
        json.loads(staged_info.read_text(encoding="utf-8"))
        self.assertEqual(len((self.root / "package/SHA256SUMS.txt").read_text().splitlines()), 3)

    def test_already_strict_json_is_not_rewritten(self):
        self.info.write_text(json.dumps(metadata()), encoding="utf-8")
        self.assertFalse(self.run_stage()["moduleinfo"]["normalization_applied"])

    def test_duplicate_json_keys_fail(self):
        self.info.write_text('{"Name":"Skell-Delay","Name":"Other"}', encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_invalid_json_fails(self):
        self.info.write_text('{"Name":"Skell-Delay","Classes":[,]}', encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_product_vendor_and_version_must_match(self):
        for kwargs in ({"product": "Other"}, {"vendor": "Other Audio"}, {"version": "2.0.0"}):
            with self.subTest(kwargs=kwargs):
                self.info.write_text(render(**kwargs), encoding="utf-8")
                with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_class_ids_and_categories_must_be_valid_and_unique(self):
        self.info.write_text(render(controller="ABCDEF019182FAEB536B6C79446C6179"), encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()
        self.info.write_text(render(controller="bad"), encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()
        self.info.write_text(render().replace("Component Controller Class", "Unknown"), encoding="utf-8")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_pe_must_be_amd64_pe32_plus_dll(self):
        for image in (pe(machine=0x14C), pe(magic=0x10B), pe(signature=b"PE\\\\0\\\\0"),
                      pe(sections=0), pe(dll=False)):
            with self.subTest(image=image[128:154]):
                self.module.write_bytes(image)
                with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_rejects_extra_module_and_non_vst3_payloads(self):
        (self.bundle / "extra.vst3").write_bytes(pe())
        with self.assertRaises(GATE.PackageError): self.run_stage()
        (self.bundle / "extra.vst3").unlink()
        (self.bundle / "unexpected.exe").write_bytes(b"MZ")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_rejects_multiple_bundles_and_stale_staging(self):
        (self.bundle.parent / "Other.vst3").mkdir()
        with self.assertRaises(GATE.PackageError): self.run_stage()
        (self.bundle.parent / "Other.vst3").rmdir()
        (self.root / "package").mkdir()
        (self.root / "package/stale.txt").write_text("stale")
        with self.assertRaises(GATE.PackageError): self.run_stage()

    def test_requires_full_commits_and_pluginval_ten(self):
        with self.assertRaises(GATE.PackageError):
            GATE.stage(self.bundle, self.root / "p1", self.cmake, "abc", "2"*40, "439", 10)
        with self.assertRaises(GATE.PackageError):
            GATE.stage(self.bundle, self.root / "p2", self.cmake, "1"*40, "2"*40, "439", 5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
