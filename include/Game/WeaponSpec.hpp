#pragma once

#include <cstddef>
#include <cstdint>

#include "Game/AimArchitecture.hpp"

namespace OW {

const WeaponSpec* ResolveWeaponSpec(uint64_t heroId, int action = 0);
const WeaponSpec* ResolveDefaultWeaponSpec(uint64_t heroId);
const WeaponSpec* WeaponSpecsBegin();
const WeaponSpec* WeaponSpecsEnd();
std::size_t WeaponSpecCount();

const char* AimClassName(AimClass value);
const char* AimBehaviorName(AimBehaviorType value);
const char* FirePolicyName(FirePolicyType value);
const char* PredictionOverrideName(PredictionOverrideMode value);

} // namespace OW
