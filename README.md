# Skell-Delay

by Skelly Audio

A Windows VST3-only delay plugin built with JUCE 8: tape, BBD, shimmer, reverse and ping-pong echoes, plus performance-aware features (Elastic, Pocket, Telepathy, Ecology), grain and spectral echoes, performance buttons, a preset browser, undo/redo, and a resizable, accessible interface.

- Manual: `docs/MANUAL.md`
- Release checklist (what is verified, what is not): `docs/RELEASE_CHECKLIST.md`
- Tests: `tests/` (see `tests/README.md`)
- Installers and CI: `installer/`, `.github/workflows/release.yml` (not yet run)
- Legal / naming decisions: `legal/README.md`
- Beta kit: `beta/`

Build (Windows x64): `cmake -S . -B build -A x64 -DSKELL_BUILD_TESTS=ON && cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
