#pragma once

#include <cstdint>

#include "Game/AimArchitecture.hpp"

namespace OW {

const HeroGeometrySpec* ResolveHeroGeometrySpec(uint64_t heroId);
const BoneHitboxSpec* ResolveBoneHitboxSpec(uint64_t heroId, int boneId);
float ResolveBoneHitboxRadius(uint64_t heroId, int boneId, float fallbackRadius);
float ResolveEffectiveHitWindow(uint64_t heroId,
                                int boneId,
                                const WeaponSpec* weapon,
                                float hitboxScalePercent,
                                float fallbackBoneRadius);
int AimBoneToSkeletonBoneId(int aimBone);

} // namespace OW
