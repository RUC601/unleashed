# Annotated Source: include/Game/Target.hpp

Source: D:/Desktop/SenseZen/ECS_O/01_PRODUCTS/un-dma/include/Game/Target.hpp
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10.
Purpose: reading notes only, not runtime source of truth.

EN: This is not a copy of the full file. It annotates the most important target/prediction blocks in the current snapshot.
中文：这不是完整文件副本。它只批注当前快照里最重要的目标选择和预测代码块。

## Mental Model

EN: `Target.hpp` turns entity snapshots and config state into an aim point and eventually input movement.
中文：`Target.hpp` 把实体快照和配置状态转换成瞄点，后续再转换成输入移动。

```text
EN: Snapshot entities -> resolve weapon/action -> resolve projectile runtime data -> predict -> score target.
中文：快照实体 -> 解析武器/action -> 解析运行时弹丸数据 -> 预测 -> 给目标打分。
```

## Projectile Runtime Spec

```cpp
inline ProjectileRuntimeSpec ResolveProjectileRuntimeSpec(const WeaponSpec* weapon,
                                                          const c_entity& local,
                                                          bool secondary) {
    if (Config::hanzoautospeed &&
        local.HeroID == eHero::HERO_HANJO) {
        Config::predit_level = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
        if (local.skill2act) Config::predit_level = 110.f;
    }

    const float fallbackSpeed = Config::predit_level;
    const bool fallbackGravity = Config::projectile_arc ||
        local.HeroID == eHero::HERO_HANJO ||
        Config::Gravitypredit;
```

EN: The function starts by merging a special Hanzo runtime read with legacy prediction config.
中文：这个函数一开始会把 Hanzo 的特殊运行时读取和旧版预测配置合并。

EN: `fallbackSpeed` is not the preferred modern path. It is the legacy escape hatch when `WeaponSpec` does not provide usable projectile speed.
中文：`fallbackSpeed` 不是现代优先路径。它是 `WeaponSpec` 没有提供可用弹速时的旧版兜底。

```cpp
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
}
```

EN: The boolean tells later diagnostics whether the value came from reviewed weapon data.
中文：这个布尔值会告诉后续诊断：当前弹速/重力是否来自整理过的武器数据。

EN: If a projectile hero feels wrong, first check whether diagnostics say `source=weapon` or `source=legacy`.
中文：如果某个弹丸英雄手感不对，先看诊断里是 `source=weapon` 还是 `source=legacy`。

## Lead Prediction

```cpp
inline LeadPredictionResult ResolveLeadPrediction(c_entity& entity,
                                                  Vector3 rawAimPoint,
                                                  const ProjectileRuntimeSpec& projectile,
                                                  bool predictionEnabled,
                                                  bool secondary,
                                                  float extraPreFireDelayMs = 0.0f) {
    LeadPredictionResult result{};
    result.predictionEnabled = predictionEnabled;
    result.rawAimPoint = rawAimPoint;
    result.preFireAimPoint = rawAimPoint;
    result.finalAimPoint = rawAimPoint;
    result.projectile = projectile;

    if (!predictionEnabled || IsZeroVector(rawAimPoint))
        return result;
```

EN: The result object is initialized to "no movement from raw point". Early return is therefore safe.
中文：`result` 初始状态就是“不改变 raw point”。所以早退是安全的。

EN: This makes disabled prediction and invalid aim points behave predictably: final aim equals raw aim.
中文：这样在关闭预测或瞄点无效时，行为很可预期：最终瞄点等于原始瞄点。

```cpp
    result.distance = camera.DistTo(rawAimPoint);
    const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
    result.targetVelocity = AccelerationAwareVelocity(entity, result.distance, projectile.projectileSpeed);
    result.timing = EstimateLeadTimingForAimPoint(rawAimPoint, secondary);
    result.preFireAimPoint = ApplyTargetMotionPreFireDelay(
        rawAimPoint,
        result.targetVelocity,
        result.timing.preFireDelayMs,
        preFireMaxMs);
    result.finalAimPoint = result.preFireAimPoint;
```

EN: The code separates motion estimation from timing. Velocity tells where the target is going; timing tells how long the system waits before the shot matters.
中文：代码把运动估计和时间估计划开。速度说明目标往哪里走，时间说明系统要等多久这个开火才真正发生。

EN: `preFireAimPoint` is already adjusted before projectile travel correction happens.
中文：在弹丸飞行修正之前，`preFireAimPoint` 已经先按开火前延迟调整过。

```cpp
    if (projectile.projectileSpeed > 0.0f) {
        const float leadDistance = camera.DistTo(result.preFireAimPoint);
        if (secondary) {
            AimCorrection22(&result.finalAimPoint, result.targetVelocity, leadDistance, projectile.projectileSpeed, projectile.gravity);
        } else {
            AimCorrection(&result.finalAimPoint, result.targetVelocity, leadDistance, projectile.projectileSpeed, projectile.gravity);
        }
    }
```

EN: Projectile correction only runs when projectile speed is positive.
中文：只有弹速为正时，才会做弹丸飞行修正。

EN: The secondary path uses a separate correction helper, so primary and secondary action bugs may have different causes.
中文：secondary 路径使用另一个修正 helper，所以主/副 action 的问题可能不是同一个原因。

## AcquireTarget Start

```cpp
inline TargetCandidate AcquireTarget(bool predit = false, bool ignoreInvisible = Config::aimbotIgnoreInvisible) {
    TargetCandidate best{};
    Vector2 CrossHair = TargetingDetail::CrosshairCenter();
    const Matrix aimViewMatrix = OW::SnapshotViewMatrix();
    const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
    auto entities = TargetingDetail::SnapshotEntities();
    auto hp_dy_entities = TargetingDetail::SnapshotDynamicEntities();
    auto local_entity = TargetingDetail::SnapshotLocalEntity();
    const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::aimbotAttack);
```

EN: The function begins by snapshotting everything it needs. That is deliberate; it avoids reading moving global state repeatedly during scoring.
中文：这个函数一开始就把需要的状态都快照下来。这是有意的，避免打分过程中反复读取变化中的全局状态。

EN: `Config::aimbotAttack` selects the weapon/action row for the current local hero.
中文：`Config::aimbotAttack` 会为当前本地英雄选择武器/action 行。

```cpp
const ProjectileRuntimeSpec projectileSpec =
    TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, false);
for (size_t i = 0; i < entities.size(); i++) {
    const bool teamPass = TargetingDetail::TargetTeamMatches(
        entities[i], Config::aimbotTeam, local_entity);
    if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
        if (ignoreInvisible && !entities[i].Vis &&
            !TargetingDetail::CandidateCanBypassTrace(lockPolicy, activeLock, candidateKey)) {
            continue;
        }
```

EN: Candidate filtering happens before expensive scoring. Team, validity, visibility, and lock policy are gates.
中文：候选过滤发生在昂贵打分之前。队伍、有效性、可见性和锁定策略都是门槛。

EN: The lock policy can allow a current target to bypass some trace checks, which helps avoid flickering off an existing lock.
中文：锁定策略可以允许当前目标绕过部分 trace 检查，用来避免已经锁住的目标频繁闪断。

```cpp
const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
    entities[i],
    RootPos,
    projectileSpec,
    resolvedPrediction,
    false);
PreditPos = lead.finalAimPoint;
const Vector3 fovPoint = lead.finalAimPoint;
if (TargetingDetail::IsWithinFovDeg(fovContext, fovPoint, Config::Fov, &fovScoreDeg)) {
    const float distance = TargetingDetail::CameraPosition().DistTo(RootPos);
    float score;
    if (Config::aimbotPriority == 0) {
        score = fovScoreDeg;
    } else if (Config::aimbotPriority == 1) {
        score = entities[i].PlayerHealth;
    } else {
        score = distance;
    }
}
```

EN: FOV is tested against the predicted final aim point, not only the raw bone.
中文：FOV 检查使用的是预测后的最终瞄点，而不仅是原始骨骼点。

EN: Priority mode changes the scoring variable, but all candidates still have to pass the same basic gates first.
中文：优先级模式改变的是打分变量，但所有候选仍然必须先通过基础门槛。

## Common Misread

EN: `predit` is not the only switch that decides prediction. The current weapon spec and `Config::aimbotPredictionMode` are also part of the decision.
中文：`predit` 不是唯一决定是否预测的开关。当前武器 spec 和 `Config::aimbotPredictionMode` 也参与决策。

EN: When a target is not selected, check gates in this order: runtime validity, team mode, visibility/trace, min lock, distance, prediction-adjusted FOV, then priority score.
中文：当某个目标没被选中时，按这个顺序查：运行时有效性、队伍模式、可见性/trace、最小锁定、距离、预测后的 FOV、最后才是优先级分数。
