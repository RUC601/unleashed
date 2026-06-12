# 03 Aim, Prediction, And Fire Chain

Source: D:/Desktop/ClaudeCodexCoding/Unleashed/include/Game/Target.hpp; D:/Desktop/ClaudeCodexCoding/Unleashed/src/Game/WeaponSpec.cpp; D:/Desktop/ClaudeCodexCoding/Unleashed/src/Game/HeroSkills.cpp
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10.
Purpose: reading notes only, not runtime source of truth.

EN: This note explains the current path from weapon definition to predicted aim point.
中文：这篇笔记解释当前从武器定义到预测瞄点的路径。

## Short Chain

```text
local_entity.HeroID + Config::aimbotAttack
  -> ResolveWeaponSpec(...)
  -> ResolveProjectileRuntimeSpec(...)
  -> ResolveLeadPrediction(...)
  -> FOV / distance / lock scoring
  -> selected TargetCandidate
```

EN: `WeaponSpec` describes the static weapon/action profile. `Target.hpp` combines that static profile with runtime state.
中文：`WeaponSpec` 描述静态武器/action 配置；`Target.hpp` 把静态配置和运行时状态合并。

EN: Prediction is not just projectile speed. It also includes motion estimation and pre-fire timing.
中文：预测不只是弹速。它还包含目标运动估计和开火前延迟。

## Weapon Lookup

```cpp
const WeaponSpec* ResolveWeaponSpec(uint64_t heroId, int action)
{
    const WeaponSpec* firstForHero = nullptr;
    for (const WeaponSpec& spec : kWeaponSpecs) {
        if (spec.heroId != heroId)
            continue;
        if (!firstForHero)
            firstForHero = &spec;
        if (spec.action == action)
            return &spec;
    }
    return firstForHero;
}
```

EN: The resolver first tries the exact hero/action pair, then falls back to the first weapon for that hero.
中文：解析器优先找精确的 hero/action 组合，找不到时回退到该英雄的第一条武器记录。

EN: This fallback is useful for resilience, but it can hide missing action-specific data.
中文：这个回退提高鲁棒性，但也可能掩盖某个 action 缺少专用数据的问题。

## Runtime Projectile Spec

```cpp
const float fallbackSpeed = Config::predit_level;
const bool fallbackGravity = Config::projectile_arc ||
    local.HeroID == eHero::HERO_HANJO ||
    Config::Gravitypredit;

if (weapon && weapon->projectile.projectileSpeed > 0.0f) {
    return ProjectileRuntimeSpec{
        weapon->projectile.projectileSpeed,
        weapon->projectile.gravity,
        true
    };
}

return ProjectileRuntimeSpec{
    fallbackSpeed,
    fallbackGravity,
    false
};
```

EN: Static `WeaponSpec` projectile data wins when it has a valid speed. Legacy config values are fallback.
中文：当静态 `WeaponSpec` 有有效弹速时，它优先。旧配置值只是 fallback。

EN: `fromWeaponSpec` is important in diagnostics because it tells you whether the aim calculation used reviewed weapon data or legacy sliders.
中文：`fromWeaponSpec` 对诊断很重要，因为它告诉你瞄准计算用的是已整理武器数据，还是旧滑条 fallback。

## Lead Solve

```cpp
const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
result.targetVelocity = AccelerationAwareVelocity(entity, result.distance, projectile.projectileSpeed);
result.timing = EstimateLeadTimingForAimPoint(rawAimPoint, secondary);
result.preFireAimPoint = ApplyTargetMotionPreFireDelay(
    rawAimPoint,
    result.targetVelocity,
    result.timing.preFireDelayMs,
    preFireMaxMs);

if (projectile.projectileSpeed > 0.0f) {
    const float leadDistance = camera.DistTo(result.preFireAimPoint);
    AimCorrection(&result.finalAimPoint, result.targetVelocity, leadDistance, projectile.projectileSpeed, projectile.gravity);
}
```

EN: Lead solving has two phases: move the raw point for pre-fire delay, then apply projectile travel correction.
中文：提前量计算有两段：先按开火前延迟移动 raw point，再按弹丸飞行时间做修正。

EN: Hitscan weapons or zero projectile speed can still use pre-fire logic, but they do not need projectile travel correction.
中文：即时命中或弹速为零的武器仍可能使用 pre-fire 逻辑，但不需要弹丸飞行修正。

## Target Selection

```cpp
auto entities = TargetingDetail::SnapshotEntities();
auto local_entity = TargetingDetail::SnapshotLocalEntity();
const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::aimbotAttack);
const ProjectileRuntimeSpec projectileSpec =
    TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, false);

for (size_t i = 0; i < entities.size(); i++) {
    if (TargetingDetail::IsRuntimeTargetValid(entities[i], false)) {
        const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
            entities[i],
            RootPos,
            projectileSpec,
            resolvedPrediction,
            false);
        if (TargetingDetail::IsWithinFovDeg(fovContext, lead.finalAimPoint, Config::Fov, &fovScoreDeg)) {
            // candidate can be scored
        }
    }
}
```

EN: Target selection evaluates predicted aim points, not only raw bone positions.
中文：目标选择评估的是预测后的瞄点，不只是原始骨骼位置。

EN: This means prediction can change both where the aim goes and which target wins the FOV/priority contest.
中文：这意味着预测不仅会改变瞄准位置，也可能改变哪个目标在 FOV/优先级竞争中胜出。

## Fire Path Note

EN: Fire behavior is split. `WeaponSpec` stores a default fire policy, config/preset state decides the active policy, and target/hero-skill runtime code executes the actual output.
中文：开火行为是拆开的。`WeaponSpec` 存默认 fire policy，config/preset 决定当前 active policy，target/hero-skill 运行时代码执行真正输出。

EN: When checking a hero action, read the weapon row, the active hero preset, and the runtime helper together.
中文：检查某个英雄 action 时，要把武器行、当前英雄 preset 和运行时 helper 一起读。
