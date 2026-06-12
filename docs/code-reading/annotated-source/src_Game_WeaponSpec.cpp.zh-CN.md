# Annotated Source: src/Game/WeaponSpec.cpp

Source: D:/Desktop/ClaudeCodexCoding/Unleashed/src/Game/WeaponSpec.cpp
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10.
Purpose: reading notes only, not runtime source of truth.

EN: This note explains the weapon spec table and lookup helpers.
中文：这篇笔记解释武器 spec 表和查询 helper。

## Why This File Matters

EN: This file is the reviewable weapon/action data layer. It is easier to audit than scattered hard-coded hero checks.
中文：这个文件是可审查的武器/action 数据层，比散落在各处的英雄硬编码更容易检查。

EN: Runtime code still decides active profile and behavior, but `WeaponSpec.cpp` supplies default facts such as projectile speed, radius, aim class, and fire policy.
中文：运行时代码仍然决定当前 active profile 和行为，但 `WeaponSpec.cpp` 提供弹速、半径、瞄准类别、默认开火策略等基础事实。

## Constructors And Helpers

```cpp
constexpr ProjectileSpec Hitscan(float radius)
{
    return ProjectileSpec{ false, 0.0f, false, radius, 0.0f, 0.0f, true };
}

constexpr ProjectileSpec Projectile(float speed, float radius, bool gravity = false)
{
    return ProjectileSpec{ true, speed, gravity, radius, 0.0f, 0.0f, true };
}

constexpr ProjectileSpec ChargedProjectile(float minSpeed, float maxSpeed, float radius, bool gravity = false)
{
    return ProjectileSpec{ true, maxSpeed, gravity, radius, minSpeed, maxSpeed, true };
}
```

EN: These helpers make the table compact while preserving meaning.
中文：这些 helper 让表格更紧凑，同时保留语义。

EN: `Hitscan` uses speed `0.0f`; that does not mean missing data. It means projectile lead correction should not run.
中文：`Hitscan` 的速度是 `0.0f`，这不是缺数据，而是表示不需要弹丸提前量修正。

```cpp
constexpr WeaponSpec W(uint64_t heroId,
                       std::string_view heroName,
                       std::string_view weaponId,
                       std::string_view weaponName,
                       int action,
                       int order,
                       AC aimClass,
                       ProjectileSpec projectile,
                       AB behavior,
                       FP firePolicy,
                       float confidence = 0.75f,
                       std::string_view note = kReviewSource)
```

EN: Each row combines hero identity, UI/action identity, aim style, projectile facts, behavior defaults, and provenance.
中文：每一行把英雄身份、UI/action 身份、瞄准风格、弹丸事实、默认行为和来源说明放在一起。

EN: `confidence` and `note` are important because not every row has the same evidence quality.
中文：`confidence` 和 `note` 很重要，因为不是每一行都有同等质量的证据。

## Table Rows

```cpp
constexpr std::array<WeaponSpec, 74> kWeaponSpecs = {
    W(eHero::HERO_REAPER, "Reaper", "reaper_hellfire_shotguns", "Hellfire Shotguns", 0, 1, AC::Shotgun, Hitscan(0.04f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_TRACER, "Tracer", "tracer_pulse_pistols", "Pulse Pistols", 0, 1, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_HANJO, "Hanzo", "hanzo_storm_bow", "Storm Bow", 0, 1, AC::ProjectileSingle, ChargedProjectile(25.0f, 110.0f, 0.125f, true), AB::FlickDelay, FP::ChargeRelease),
};
```

EN: The real table has many more rows. This snippet shows the shape, not the full data.
中文：真实表格有更多行。这个片段只展示结构，不复制完整数据。

EN: The `action` field is the bridge to `Config::aimbotAttack`. A hero can have multiple rows for primary, secondary, scoped, or alternate actions.
中文：`action` 字段是通向 `Config::aimbotAttack` 的桥。一个英雄可以有主武器、副武器、开镜或其他 action 的多行记录。

EN: The current snapshot includes newer heroes such as Emre, Sierra, Wuyang, and Jetpack Cat in this table.
中文：当前快照里，这张表已经包含 Emre、Sierra、Wuyang、Jetpack Cat 等较新的英雄记录。

## Resolver

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

EN: The resolver returns the exact action when possible and otherwise falls back to the first row for that hero.
中文：解析器能找到精确 action 就返回精确行，否则回退到该英雄第一行。

EN: This fallback keeps runtime code from failing hard, but it can also make a missing action look superficially functional.
中文：这个回退可以防止运行时代码直接失败，但也可能让缺失 action 看起来“好像能用”。

## Label Helpers

```cpp
bool HeroUsesScopedStanceActions(uint64_t heroId)
{
    return HeroHasAttackAction(heroId, 0) && HeroHasAttackAction(heroId, 2);
}

const char* AttackActionNameForHero(uint64_t heroId, int action)
{
    if (HeroUsesScopedStanceActions(heroId) && action == 0)
        return "Unscoped";
    return Labels::AttackActionName(action);
}
```

EN: Scoped heroes can show `Unscoped` instead of a generic primary label.
中文：有开镜姿态的英雄可以显示 `Unscoped`，而不是泛泛的 primary 标签。

EN: This is a UI/readability layer. It does not by itself change runtime firing behavior.
中文：这是 UI/可读性层，本身不直接改变运行时开火行为。

## How To Add Or Audit A Row

EN: For a new row, check four things together: hero ID, action number, projectile facts, and behavior/fire policy defaults.
中文：新增或审查一行时，要同时检查四件事：英雄 ID、action 编号、弹丸事实、behavior/fire policy 默认值。

EN: Then verify where the row is consumed: `ResolveWeaponSpec(...)`, `ResolveProjectileRuntimeSpec(...)`, target scoring, and any hero-skill path that calls lead prediction.
中文：然后验证消费路径：`ResolveWeaponSpec(...)`、`ResolveProjectileRuntimeSpec(...)`、目标打分，以及任何调用提前量预测的 hero-skill 路径。
