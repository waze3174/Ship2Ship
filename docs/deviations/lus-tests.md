# ComboShip deviations — libultraship's unit tests (`lus_tests`)

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

`lus_tests` is libultraship's gtest suite (`libultraship/tests/`). Upstream runs it in its own **Debug** build
(`libultraship/.github/workflows/test-validation.yml`). We run it in the Linux CI job's **Release**, `COMBO_BUILD`
configuration (`.github/workflows/build-linux.yml`, step "libultraship unit tests (lus_tests)"), which upstream never
does. The entries below are the vendored test files we changed so the suite tests the build it actually runs against.
The first CI run (45784875958, 2026-11-26) had 5 of 437 failing; each entry names the test it fixed.

## Tests follow the library's Debug-only `_DEBUG` (2026-11-26)

**Fixed:** `EventSystem.CallEventUpdatesCallerMetadata`, `EventSystem.CallEventIncrementsCaller`.

**Why:** `EventSystem::CallEvent` records caller metadata (`EventRegistration::Callers`) only under `#ifdef _DEBUG`
("Event Debugger is a dev tool and shouldn't be compiled into Releases", `src/ship/events/EventSystem.cpp`). That is
upstream behaviour, unchanged by us. Upstream's tests assume a Debug build, so in our Release run `Callers` stayed empty:
`find` returned `end()` and `at()` threw `unordered_map::at`.

**`libultraship/tests/CMakeLists.txt`:** adds `$<$<CONFIG:Debug>:_DEBUG>` to `lus_tests`, mirroring the library's own
PRIVATE definition (`src/CMakeLists.txt`). Besides the two tests, this keeps the tests' view of `Context` the same as
the library's: `Context.h` has a `#ifndef _DEBUG` member (`mLogThreadPool`), so a GCC Debug build without it compiled
the tests against a different `Context` layout.

**`libultraship/tests/event_system_tests.cpp`:** both tests check the metadata under `#ifdef _DEBUG` and, in other
builds, that `Callers` stays empty (the Release contract).

**The game:** not affected. Release builds never had caller metadata, upstream included.
