# Save compatibility implementation — November 4, 2026

Implemented in PR #62's audited checkout (`pr/economy-overhaul`, based on 90a11ac77).

## Behavior

- PC Quick Save uses a bounded PECP container containing the native save and full companion payload, with checksums and a native US save-v1 identity. Atomic publication retains the previous checkpoint on failure.
- Quick Load admits embedded companion data and native inventory/tail structure before committing. Legacy MCD imports still require their referenced companion. An existing corrupt PECP never silently selects an older MCD. A slot holding only an older `quicksave_<slot>.mcd` shows as "Saved, older format" and loads after its native checks and admission; saving to that slot writes the checkpoint, which then takes over, and leaves the MCD on disk unused. Builds with the companion store could not write these files (their blank save name fails the companion path check), so older quick saves carry no companion link; a link they can't resolve is refused.
- PS5 file checkpoints use PEHYB001: a required PECP component plus optional exact runtime bytes. Required disc identity and complete framing/checksums are validated first. Matching supported runtime owners may restore exactly; changed builds, settings, sessions, rooms, assets or unsupported split-provider layouts select a saved-room restart. A failed rollback never falls through to portable commit. Legacy partial component files have no portable contract and remain refused by the new file-load path.
- Startup activation stays with the current launch. Unsupported optional provider data remains opaque and preserved; its hooks, services and menu mutations are unavailable while its state is locked.
- Native records prepare checked migration candidates before load commit. Shipped record callbacks now use the supplied candidate rather than writing their globals during preparation. Failed migration or validation preserves the original valid record through later saves and legacy-window writes.
- Companion sections support bounded scratch upgrades and checks under the registry's read-only callback scope. Required host inventory identity validates its structure and native pool fingerprint before commit.
- The host owns the legacy `pe_stacks.counts` record even without Stacks linked. Legacy and companion reserves materialize plain copies as space opens while Stacks is absent or All Mods is Off. Overflow survives without a visible anchor. A companion supersedes a stale legacy record. Re-enabling grouping does not delete anchorless reserves; new stacks refuse reservation beyond the saved schema's pair capacity.
- Replayed profile receipts check ownership before requesting promotion, avoiding repeated publication of an already-owned unlock.

## PS5 source preparation

The original archive overlay remains pinned. A separately hashed current-host compatibility closure is applied afterward, including split providers, persistence sources and their headers. Obsolete grouped provider sources are removed only from the fresh export. The source-cohort identity includes the export recipe. Direct-source and exported checkpoint UI describe exact-versus-room-restart behavior.

The pre-split Loot and Catalog private runtime serializers reference layouts that no longer exist; those optional owners refuse exact serialization and use persistent recovery. Their old private fields are not interpreted as current state. Native Wardrobe retains built-in costumes and profile keys; desktop ZIP packs remain unavailable in the native build without pulling desktop download dependencies into it.

## Verification

- Desktop `parasite-eve-pc` builds successfully.
- **18/18 focused CTests pass**, including all eight original preservation/recovery scenarios, portable checkpoint admission/publication, rejected migration round trips, legacy and companion reserve conservation, inventory identity refusal, repeated receipt replay, native inventory, crash-tail, economy store, save records and loot parity.
- The PS5 session test exercises production session selection and the real runtime transaction engine with platform/game boundaries replaced by fixtures: exact restore, changed build/mod fallback, corrupt/disc/native refusal, rollback failure and legacy-partial refusal.
- PS5 compatibility export test verifies every source digest, stale-source pruning and refusal before writes.
- Native host/provider syntax checks pass with CPU-components, changed-cohort and native-GPU flags. Native state sources other than the POSIX storage adapter also pass host syntax checks.
- `git diff --check` passes.
- The first in-game Quick Load refused its own checkpoint: loads bound the full card path while the companion carries the 20-byte mapped name. Both names are now constants, and the portable checkpoint test builds its save path with the real `Psyz_AdjustPath`.
- Older quick saves: the portable checkpoint test covers slot state, exact-size reading, admission of an unlinked card, refusal of an unresolved link, and the older file surviving a save. The slot row, the toasts and `pe_load`'s order in `pe_quicksave.c` need the game and have no automated test; no real older quick save has been loaded.

Local evidence lives beside this checkout in `.port-staging/`: `compat-push-tests.log`, `compat-final-latches-build.log`, `compat-native-syntax.log`, `compat-native-state-syntax.log`, and broader build/test logs.

## Verification limits

The complete unrelated project suite is not green in this Windows/Clang environment. Its broad run reported 35 failures/not-run results out of 165 tests (before fixing the loot reset regression, whose parity test now passes). Several legacy fixture executables cannot link locally; actor-animation, player-input-assets and GPU descriptor probes also reported failures. These results are not represented as passing or certified as pre-existing.

No PS5 SDK link, console run, or real-game save/play/load walkthrough was performed. The implementation and host tests do not constitute PS5 hardware or full gameplay certification. The supported native save identity remains US save-v1; unknown required formats refuse safely.
