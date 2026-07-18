#pragma once

#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <iterator>

namespace OW::Labels {

    inline constexpr const char* kAimActivationKeys[] = {
        "Right Mouse", "Left Mouse", "Mouse 4", "Mouse 5", "Left Shift", "Left Alt",
        "V Key", "Left Ctrl", "Tab", "E Key", "Q Key", "F Key", "CapsLock", "Space",
        "None"
    };

    inline constexpr int kAimActivationKeyVks[] = {
        VK_RBUTTON, VK_LBUTTON, VK_XBUTTON1, VK_XBUTTON2, VK_LSHIFT, VK_LMENU,
        0x56, VK_LCONTROL, VK_TAB, 0x45, 0x51, 0x46, VK_CAPITAL, VK_SPACE,
        0
    };

    static_assert(std::size(kAimActivationKeys) == std::size(kAimActivationKeyVks),
                  "Activation key labels and VK bindings must stay aligned.");
    inline constexpr int kAimActivationKeyNoneIndex =
        static_cast<int>(std::size(kAimActivationKeys)) - 1;
    static_assert(kAimActivationKeyVks[kAimActivationKeyNoneIndex] == 0,
                  "The final activation-key entry must remain None.");

    // Skill activation may listen to side buttons, but the persisted skill-key
    // catalog can safely emit only left/right mouse plus keyboard HID usages.
    // Keep a mapped list so the Skill Key UI cannot offer a value that no-ops.
    inline constexpr const char* kHeroSkillOutputKeys[] = {
        "Right Mouse", "Left Mouse", "Left Shift", "Left Alt", "V Key",
        "Left Ctrl", "Tab", "E Key", "Q Key", "F Key", "CapsLock", "Space"
    };

    inline constexpr int kHeroSkillOutputHotkeys[] = {
        0, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
    };

    static_assert(std::size(kHeroSkillOutputKeys) == std::size(kHeroSkillOutputHotkeys),
                  "Hero skill output labels and stored hotkey values must stay aligned.");

    inline constexpr const char* kAimModes[] = {
        "Tracking", "Flick"
    };

    inline constexpr const char* kTriggerbotModes[] = {
        "Hold", "Toggle", "Always"
    };

    inline constexpr const char* kAttackActions[] = {
        "Primary Fire", "Secondary Fire", "Scoped", "Unscoped",
        "Ability 1", "Ability 2", "Ability 3", "Ultimate"
    };

    inline constexpr const char* kAttackActionsCompact[] = {
        "Primary", "Secondary", "Scoped", "Unscoped",
        "Ability 1", "Ability 2", "Ability 3", "Ultimate"
    };

    template <std::size_t Count>
    inline int LabelCount(const char* const (&)[Count]) {
        return static_cast<int>(Count);
    }

    template <std::size_t Count>
    inline const char* LabelAt(const char* const (&items)[Count], int index, const char* fallback = "Invalid") {
        if (index < 0 || index >= static_cast<int>(Count))
            return fallback;
        return items[static_cast<std::size_t>(index)];
    }

    inline int AimActivationKeyCount() {
        return LabelCount(kAimActivationKeys);
    }

    inline int AimModeCount() {
        return LabelCount(kAimModes);
    }

    inline int HeroSkillOutputKeyCount() {
        return static_cast<int>(std::size(kHeroSkillOutputKeys));
    }

    inline constexpr bool IsHeroSkillOutputHotkey(int hotkey) {
        for (const int candidate : kHeroSkillOutputHotkeys) {
            if (candidate == hotkey)
                return true;
        }
        return false;
    }

    inline int TriggerbotModeCount() {
        return LabelCount(kTriggerbotModes);
    }

    inline int AttackActionCount() {
        return LabelCount(kAttackActions);
    }

    inline const char* AimActivationKeyName(int keyIndex) {
        return LabelAt(kAimActivationKeys, keyIndex);
    }

    inline int AimActivationKeyVk(int keyIndex) {
        if (keyIndex < 0 || keyIndex >= static_cast<int>(std::size(kAimActivationKeyVks)))
            return 0;
        return kAimActivationKeyVks[static_cast<std::size_t>(keyIndex)];
    }

    template <typename IsVkDown>
    inline bool TryMatchAimActivationKey(int keyIndex,
                                         IsVkDown&& isVkDown,
                                         int* matchedKeyIndex = nullptr) {
        if (matchedKeyIndex)
            *matchedKeyIndex = keyIndex;

        const int vk = AimActivationKeyVk(keyIndex);
        return vk > 0 && isVkDown(vk);
    }

    inline const char* AimModeName(int mode) {
        return LabelAt(kAimModes, mode);
    }

    inline const char* TriggerbotModeName(int mode) {
        return LabelAt(kTriggerbotModes, mode);
    }

    inline const char* AttackActionName(int action) {
        return LabelAt(kAttackActions, action);
    }

    inline const char* AttackActionCompactName(int action) {
        return LabelAt(kAttackActionsCompact, action);
    }

} // namespace OW::Labels
