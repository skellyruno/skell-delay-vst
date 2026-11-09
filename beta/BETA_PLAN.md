# Closed beta plan (3 weeks, 20-30 testers)

**Who:** mostly vocal producers and mix engineers (rap / pop / R&B), a few sound designers, at least 5 on Windows and 5 on Mac, a mix of hosts (Ableton, Logic, FL Studio, Reaper, Cubase, Studio One), and at least two with a small laptop or high-DPI screen.

**Week 1 - does it work?** Install, load, save/reload a project, automate, bounce. Use the *Smoke test* in `TEST_MATRIX.csv`.
**Week 2 - does it sound good?** Use it on a real session every day. Questions in `FEEDBACK_FORM.md`. Ask each person to try the four "smart" features on their own voice and tell you where each one fails.
**Week 3 - polish.** Fix, re-send one build, ask "would you pay for it, and how much?"

**What you give testers:** the signed installer, `docs/MANUAL.md`, `FEEDBACK_FORM.md`, `BUG_REPORT.md`, and a private chat or email address.

**Exit criteria for release:**
- No crash or hang reported in two consecutive builds.
- Every host in the test matrix has at least two clean runs on each OS.
- At least 70% of testers say the smart features worked on their material at least some of the time, and nobody reports them doing something harmful.
- Their answer on price is clear enough to set it.

**Where host crash logs are:** Windows: Event Viewer -> Windows Logs -> Application, plus the host's own log folder. macOS: Console.app -> Crash Reports (`~/Library/Logs/DiagnosticReports`). Ask testers to attach the newest one.
