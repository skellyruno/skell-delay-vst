from pathlib import Path
import unittest


WORKFLOW_PATH = Path(__file__).resolve().parents[1] / ".github" / "workflows" / "release.yml"


def workflow_step(source: str, name: str) -> str:
    marker = f"      - name: {name}\n"
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"workflow step not found: {name}")
    next_step = source.find("\n      - name:", start + len(marker))
    return source[start:] if next_step < 0 else source[start:next_step]


class ReleaseWorkflowContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = WORKFLOW_PATH.read_text(encoding="utf-8")

    def test_installable_windows_artifact_requires_private_repository(self):
        step = workflow_step(self.workflow, "Upload validated Windows VST3 and installer artifacts")
        self.assertIn(
            "if: runner.os == 'Windows' && github.event.repository.private == true",
            step,
        )
        self.assertIn("uses: actions/upload-artifact@v6", step)

    def test_public_repository_emits_explicit_withholding_notice(self):
        step = workflow_step(self.workflow, "Explain withheld public preview")
        self.assertIn("github.event.repository.private != true", step)
        self.assertIn("no product download artifact will be uploaded", step)

    def test_failure_diagnostics_remain_separate_from_product_upload(self):
        diagnostic_names = (
            "Preserve CTest failure diagnostics",
            "Preserve Windows DSP crash dump",
            "Preserve Windows DSP executable and symbols",
        )
        for name in diagnostic_names:
            step = workflow_step(self.workflow, name)
            self.assertIn("if: failure()", step)


if __name__ == "__main__":
    unittest.main()
