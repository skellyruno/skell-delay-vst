# Skell-Delay

by Skelly Audio

A VST3 / AU delay plugin built with JUCE 8: tape, BBD, shimmer, reverse and ping-pong echoes, plus performance-aware features (Elastic, Pocket, Telepathy, Ecology), grain and spectral echoes, performance buttons, a preset browser, undo/redo, and a resizable, accessible interface.

- Manual: `docs/MANUAL.md`
- Release checklist (what is verified, what is not): `docs/RELEASE_CHECKLIST.md`
- Tests: `tests/` (see `tests/README.md`)
- Installers and CI: `installer/`, `.github/workflows/release.yml` (not yet run)
- Legal / naming decisions: `legal/README.md`
- Beta kit: `beta/`

Build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`
