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

## Duplicate factory key: test the combo behaviour (2026-11-26)

**Fixed:** `ResourceLoader.RegisterDuplicateKeyRejected` (now `RegisterDuplicateKeyReplacesFactory` in `COMBO_BUILD`).

**Why:** the test expected upstream's rejection of a duplicate `(format, type, version)` key, which our `COMBO_BUILD`
deviation in `ResourceLoader::RegisterResourceFactory` changes on purpose: the new factory replaces the old one (see
[`resource-mgmt.md`](resource-mgmt.md), "A duplicate resource-factory key replaces the factory").

**`libultraship/tests/resource_type_tests.cpp`:** under `#ifdef COMBO_BUILD`, `RegisterDuplicateKeyReplacesFactory`
checks that the call succeeds and that the key now resolves to the new factory (through a test subclass that exposes the
protected `GetFactory`). Upstream's test is kept unchanged in the `#else` branch.

**The game:** not affected; the test now checks what the game relies on.

## Combo menu serializer: expect ABI v2 (2026-11-26)

**Fixed:** `ComboMenuExport.FlattensInDeterministicOrderWithStableIndices`.

**Why:** `combo_menu_export_tests.cpp` is our own test (no upstream copy). It was written for `CwMenu` version 1
(`53f025c01`). `ebc3587b7` moved `ComboMenuExport::Build` to version 2 when it added `CwWidget.column`, and the test was
not updated; nothing ran `lus_tests` until 2026-11-26.

**`libultraship/tests/combo_menu_export_tests.cpp`:** expects version 2 and checks each widget's `column` (A, B in
column 0, C in column 1 of the two-column sidebar), the field the bump added.

**The game:** not affected. Nothing reads `CwMenu::version`; the serializer was right and the test was stale.

## ResourceManagerScope tests destroy their Context (2026-11-26)

**Fixed:** `ResourceManagerScope.SwapsAndRestores` (SIGSEGV at exit, after the test itself had passed).

**Why:** `resource_manager_scope_tests.cpp` (our own test) made its `Context` with `CreateUninitializedInstance` and never
destroyed it, so `~Context` ran from the static destructor of `Context::mContext` at process exit. That destructor's
`__cxa_atexit` entry is registered when `libultraship.so` loads. spdlog's registry is a function-local static, built by
the first log call, so it is registered later and destroyed first. In `SwapsAndRestores` the first log call happened
inside the test (`~ResourceManager` of the swapped-in RM logs "destruct ResourceManager"); at exit `~Context` then
released the remaining RM, whose destructor logged through the destroyed registry. `NullTargetIsNoOp` destroys no RM
during the test, so its first log call happens during exit and it did not crash. The launcher and comborando hit the same
order on macOS (see the comments on `ComboExitWithoutTeardown` in `combo/ComboShip.cpp` and on `main` in
`combo/ComboRandoHeadless.cpp`).

**`libultraship/tests/resource_manager_scope_tests.cpp`:** a `ResourceManagerScope` fixture creates the uninitialized
`Context` in `SetUp` and calls `Context::DestroyInstance()` in `TearDown`, as the game's `DeinitOTR` does. The suite and
test names are unchanged. Upstream's `main` uses the same pattern (`TickableComponentTest` in
`tests/tickable_component_tests.cpp`).

**The game:** not on its normal paths. Quitting runs `SOH_Deinit` -> `DeinitOTR` -> `Context::DestroyInstance()` while
spdlog is alive, and the launcher's early exits end in `std::_Exit` (`ComboExitWithoutTeardown`). Two plain `exit(1)`
calls are left after the Context and its logger exist: the "Invalid OTR File" check in `OTRGlobals::Initialize`
(`soh/soh/OTRGlobals.cpp`) and the "Invalid O2R File" check in MM's `OTRGlobals::Initialize` (`mm/2s2h/BenPort.cpp`).
They follow an error box and would leave `~Context` to the static destructors, so on Linux and macOS they can likely end
in the same crash. Not verified in game.
