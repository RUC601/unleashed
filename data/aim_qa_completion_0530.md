# Aim Architecture QA Completion 2026-05-30

Status: completed against the default-accepted decisions in `research/aim_architecture_qa_0530.md`.

## Structure

- `WeaponSpec`: implemented in `include/Game/WeaponSpec.hpp` and `src/Game/WeaponSpec.cpp`; resolved by local hero and action slot.
- `HeroGeometrySpec` / `BoneHitboxSpec`: implemented in `include/Game/HeroGeometrySpec.hpp` and `src/Game/HeroGeometrySpec.cpp`; public-data gap recorded in `data/aim_public_hero_geometry_0530.tsv`.
- `TargetCandidate`: implemented in `include/Game/AimArchitecture.hpp`; `AcquireTarget` is the new structured path and `GetVector3` remains a compatibility wrapper.
- `EntityMotionState`: implemented and populated from target velocity in `AcquireTarget`.

## Behavior And Policy Wiring

- Prediction is resolved from `WeaponSpec` with `Auto / Force On / Force Off` config override.
- Aim behavior is separate from smoothing. `Tracking`, `Flick`, `FlickClamp`, `FlickDelay`, and `ReacquireAtApex` are persisted while legacy `Tracking/Flick` booleans remain wrapper state.
- Smoothing controller supports Linear, PID, Bezier, Piecewise, and AccelLimited. Acceleration is passed through instead of discarded.
- `SmoothY` is no longer the primary smoothing selector; `aimbotPitchScale` handles vertical/pitch scaling.
- `FirePolicy` owns manual/hold/tap/delay/burst/charge-release policy, with legacy `Autoshot` and `KeepFiring` kept for migration.
- `TargetLockPolicy` owns trace mode, unlock mode, minimum lock time, session timeout, and retarget hysteresis.
- `Max/Min Distance`, `Max Head Distance`, effective hit window, and public projectile radius are applied during target selection.

## Sequence Ownership

- Sequence execution has `ExecutionSource` and `ExecutionToken`.
- Active input sequences block global aim and trigger sources, while `SequenceInternal` is allowed to continue its own sequence.

## UI And Migration

- Aim page now exposes Aim Behavior, Prediction mode, Fire Policy, Retarget Hysteresis, Max/Min Distance, Max Aim Time, and lock controls.
- Misc page has an independent Smoothing Controller section for controller-specific parameters and Pitch Scale.
- INI/JSON hero presets persist the new fields and migrate legacy `aimMode`, `Prediction`, `Autoshot`, and `KeepFiring`.

## Public Data

- Projectile speed/radius/direct-hit/headshot table: `data/aim_public_weapon_projectiles_0530.tsv`.
- Hero/bone hitbox public source and gap table: `data/aim_public_hero_geometry_0530.tsv`.
- Source summary and unresolved public-data gaps: `data/aim_public_sources_0530.md`.

## Verification

- Build command used after each implementation round: `.\build-release.ps1`.
- Final verified output: `build\Release\Unleashed.exe`.
