# Tests

- `dsp_tests.cpp` - DSP, interface logic (typed values, undo/redo, A/B, presets, randomise), compatibility and damaged-file tests.
- `stress_tests.cpp` - long-run, extreme-setting and hostile-input tests.
- `realtime_check.cpp` - counts heap allocations inside `processBlock` and measures CPU.
- `frozen_param_ids.txt` - id, min, max, default of every released parameter. The compatibility test fails if any change. When you add a parameter, add its line (run the plugin's parameter dump); never edit or remove existing lines after release.

Run with CMake: `cmake -S . -B build -DSKELL_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build -C Release --output-on-failure`.
(That CMake target has not been built yet; these files were built and run with a hand-made g++ harness, with `-DSKELL_HARNESS=1`, which supplies a stand-in for the embedded artwork. The CMake build links the real one.)
