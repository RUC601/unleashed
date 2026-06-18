#pragma once

#include <cstdint>

namespace OW::HeroPerkRuntime {

enum class ManualOverrideMode : int {
    None = 0,
    ForceOff,
    ForceOn,
};

enum class EffectiveSource : int {
    Default = 0,
    Manual,
};

struct Snapshot {
    uint64_t heroId = 0;
    bool supported = false;
    bool effectivePerkOn = false;
    ManualOverrideMode manualOverride = ManualOverrideMode::None;
    EffectiveSource source = EffectiveSource::Default;
    const char* variantId = "";
};

void UpdateContext(uint64_t heroId, bool connected, bool teamFlag);
void ResetForDisconnect();

bool HeroSupportsManualPerk(uint64_t heroId);
bool IsEffectivePerkOn(uint64_t heroId);

void ToggleCurrentHeroManualPerk();
void ClearCurrentHeroManualPerk();
void SetManualPerkOverride(uint64_t heroId, ManualOverrideMode mode);

Snapshot CurrentSnapshot();

const char* ManualOverrideModeName(ManualOverrideMode mode);
const char* EffectiveSourceName(EffectiveSource source);

} // namespace OW::HeroPerkRuntime
