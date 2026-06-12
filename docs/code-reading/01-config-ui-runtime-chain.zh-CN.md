# 01 Config, UI, And Runtime Chain

Source: D:/Desktop/ClaudeCodexCoding/Unleashed/src/Features/UI.cpp; D:/Desktop/ClaudeCodexCoding/Unleashed/include/Utils/Config.hpp; D:/Desktop/ClaudeCodexCoding/Unleashed/src/Utils/Config.cpp
Snapshot: Git HEAD 10e87de + dirty working tree on 2026-06-10.
Purpose: reading notes only, not runtime source of truth.

EN: This note explains how a visible control becomes runtime behavior.
中文：这篇笔记解释一个界面控件如何变成真正的运行时行为。

## The Short Chain

```text
src/Features/UI.cpp
  -> edits a global Config value or a HeroPreset object
  -> writes it back through Config helpers
  -> SaveConfig / SaveHeroConfig persists it
  -> runtime code reads Config globals or active hero presets
```

EN: UI code is the front door, but `Config` is the contract.
中文：UI 代码是入口，但 `Config` 才是运行时契约。

EN: When debugging a setting, do not stop at the UI widget. Confirm the value is copied into the active preset or global runtime field.
中文：排查设置时不要停在 UI 控件。要确认值是否进入当前 active preset 或全局运行时字段。

## Important Config Surface

```cpp
inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>> heroAimPresets;
inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>> heroTriggerPresets;
inline HeroSkillPresetStore heroSkillPresets;

HeroPreset MakeHeroAimPresetFromCurrent();
HeroPreset MakeHeroTriggerPresetFromCurrent();
void ApplyHeroAimPresetToGlobals(const HeroPreset& preset);
void ApplyHeroTriggerPresetToGlobals(const HeroPreset& preset);
void SaveConfig(const std::string& path);
void LoadConfig(const std::string& path);
```

EN: `heroAimPresets` and `heroTriggerPresets` are separate stores. A value can be correct for aiming while still irrelevant to trigger behavior, or the other way around.
中文：`heroAimPresets` 和 `heroTriggerPresets` 是两套独立存储。某个值可能对瞄准正确，但对 trigger 无关，反过来也一样。

EN: `ApplyHeroAimPresetToGlobals(...)` is one of the bridges from stored preset data back into live global fields.
中文：`ApplyHeroAimPresetToGlobals(...)` 是从已保存 preset 回填到实时全局字段的桥之一。

## Preset Capture

```cpp
HeroPreset MakeHeroPresetFromCurrentUnlocked()
{
    HeroPreset preset{};
    preset.fov = Fov;
    preset.smooth = Smooth;
    preset.bone = NormalizeAimBone(Bone);
    preset.aimBehavior = aimBehavior;
    preset.aimBehaviorPresetId = aimBehaviorPresetId;
    preset.trigger.action = aimbotAttack;
    preset.trigger.mode = triggerbotMode;
    preset.trigger.key = triggerbotKey;
    return ValidateHeroPresetValue(preset);
}
```

EN: This is the "current globals to preset" direction. It answers: if the user saves now, what values become part of the hero preset?
中文：这是“当前全局值写入 preset”的方向。它回答的是：如果用户现在保存，哪些值会成为英雄 preset 的一部分？

EN: If a new UI field is not copied here or through an equivalent path, it may not survive profile or hero-slot changes.
中文：如果一个新 UI 字段没有在这里或等价路径里被复制，它可能无法跨 profile 或 hero slot 切换保留。

## Preset Apply

```cpp
void ApplyHeroAimPresetUnlocked(const HeroPreset& rawPreset)
{
    const HeroPreset preset = ValidateHeroPresetValue(rawPreset);
    Fov = preset.fov;
    Bone = NormalizeAimBone(preset.bone);
    Prediction = preset.prediction;
    aimbotPredictionMode = preset.predictionMode;
    aimbotAttack = preset.trigger.action;
    aimBehavior = preset.aimBehavior;
    aimBehaviorPresetId = preset.aimBehaviorPresetId;
    aimbotIgnoreInvisible = preset.ignoreInvisible;
    aimbotTrace = preset.traceCondition;
    ApplyAimMode(IsTrackingBehavior(preset.aimBehavior) ? 0 : 1);
}
```

EN: This is the reverse direction: selected preset to active runtime globals.
中文：这是反方向：把选中的 preset 写回当前运行时全局变量。

EN: If the UI stores a value but this apply path does not restore it, the value may appear saved but not affect the active profile.
中文：如果 UI 保存了某个值，但 apply 路径没有恢复它，这个值可能“看起来保存了”，但不会影响当前 active profile。

## Runtime Consumption

```cpp
const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::aimbotAttack);
const bool resolvedPrediction = ResolvePredictionEnabled(
    ClampPredictionOverride(Config::aimbotPredictionMode),
    weaponSpec,
    predit);
```

EN: Runtime code normally consumes simple global values such as `Config::aimbotAttack` and `Config::aimbotPredictionMode`, not the UI widgets themselves.
中文：运行时代码通常读取的是 `Config::aimbotAttack`、`Config::aimbotPredictionMode` 这样的全局值，而不是 UI 控件本身。

EN: This is why "active profile truth" matters. The runtime only sees whatever was last applied into these globals.
中文：这就是“当前 active profile 真相”重要的原因。运行时只看最后写入这些全局字段的值。

## Debug Checklist

EN: Use this checklist when a UI setting appears to do nothing.
中文：当某个 UI 设置看起来不生效时，用这个清单排查。

```text
1. Is the widget editing the intended variable or preset field?
2. Is that field validated without being clamped back to a default?
3. Is it saved to config.ini or config.heroes.json?
4. Is it loaded back during profile startup or hero-slot selection?
5. Is it applied into the globals that runtime code actually reads?
6. Is the runtime reading the same slot/action/profile you edited?
```

EN: The failure is usually at an arrow boundary, not inside the slider or checkbox itself.
中文：问题通常出在箭头边界，而不是 slider 或 checkbox 控件本身。
