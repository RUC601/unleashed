#include "Game/HeroGeometrySpec.hpp"

#include <algorithm>
#include <array>

#include "Game/Structs.hpp"
#include "Utils/Config.hpp"

namespace OW {
namespace {

constexpr std::string_view kGeometryDataNote =
    "Fallback geometry pending public per-hero/per-bone hitbox verification; do not treat as measured data.";

constexpr std::array<BoneHitboxSpec, 8> kFallbackBones = {
    BoneHitboxSpec{ BONE_HEAD, "head", 0.16f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_NECK, "neck", 0.14f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_CHEST, "chest", 0.22f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_PELVIS, "pelvis", 0.20f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_R_SHOULDER, "right_shoulder", 0.13f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_L_SHOULDER, "left_shoulder", 0.13f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_R_HAND, "right_hand", 0.10f, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_L_HAND, "left_hand", 0.10f, "sphere", "", kGeometryDataNote, 0.20f },
};

constexpr HeroGeometrySpec kFallbackGeometry{
    0,
    "fallback",
    kFallbackBones.data(),
    static_cast<int>(kFallbackBones.size())
};

} // namespace

const HeroGeometrySpec* ResolveHeroGeometrySpec(uint64_t heroId)
{
    (void)heroId;
    return &kFallbackGeometry;
}

const BoneHitboxSpec* ResolveBoneHitboxSpec(uint64_t heroId, int boneId)
{
    const HeroGeometrySpec* geometry = ResolveHeroGeometrySpec(heroId);
    if (!geometry || !geometry->bones || geometry->boneCount <= 0)
        return nullptr;

    for (int index = 0; index < geometry->boneCount; ++index) {
        const BoneHitboxSpec& bone = geometry->bones[index];
        if (bone.boneId == boneId)
            return &bone;
    }

    return nullptr;
}

float ResolveBoneHitboxRadius(uint64_t heroId, int boneId, float fallbackRadius)
{
    const BoneHitboxSpec* spec = ResolveBoneHitboxSpec(heroId, boneId);
    if (spec && spec->radiusWorld > 0.0f)
        return spec->radiusWorld;
    return (std::max)(0.0f, fallbackRadius);
}

float ResolveEffectiveHitWindow(uint64_t heroId,
                                int boneId,
                                const WeaponSpec* weapon,
                                float behaviorTolerance,
                                float fallbackBoneRadius)
{
    const float boneRadius = ResolveBoneHitboxRadius(heroId, boneId, fallbackBoneRadius);
    const float projectileRadius = weapon ? (std::max)(0.0f, weapon->prediction.projectileRadius) : 0.0f;
    return (std::max)(0.0f, boneRadius + projectileRadius + behaviorTolerance);
}

int AimBoneToSkeletonBoneId(int aimBone)
{
    switch (Config::NormalizeAimBone(aimBone)) {
    case Config::kAimBoneHead:
        return BONE_HEAD;
    case Config::kAimBoneNeck:
        return BONE_NECK;
    case Config::kAimBoneChest:
    default:
        return BONE_CHEST;
    }
}

} // namespace OW
