#include "Game/BoneSlots.hpp"
#include "Game/HeroGeometrySpec.hpp"
#include "Game/Structs.hpp"
#include "Utils/Config.hpp"

#include <cmath>
#include <cstdlib>

Memory::~Memory() = default;

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

int Fail()
{
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    const OW::HeroGeometrySpec* geometry = OW::ResolveHeroGeometrySpec(0);
    if (!geometry || geometry->boneCount != 3)
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_HEAD))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_NECK))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_CHEST))
        return Fail();
    if (OW::ResolveBoneHitboxSpec(0, OW::BONE_PELVIS))
        return Fail();
    if (!OW::IsCoreHitboxBoneId(OW::BONE_HEAD) ||
        !OW::IsCoreHitboxBoneId(OW::BONE_NECK) ||
        !OW::IsCoreHitboxBoneId(OW::BONE_CHEST) ||
        OW::IsCoreHitboxBoneId(OW::BONE_PELVIS)) {
        return Fail();
    }
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_HEAD, 0.13f), 0.16f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_NECK, 0.13f), 0.14f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_CHEST, 0.13f), 0.22f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_PELVIS, 0.13f), 0.13f))
        return Fail();
    if (!NearlyEqual(OW::ResolveEffectiveHitWindow(
            0,
            OW::BONE_HEAD,
            nullptr,
            OW::Config::kDefaultHitboxScalePercent,
            OW::Config::kLegacyDefaultHitboxRadius),
            0.16f)) {
        return Fail();
    }

    const auto cassidyCore =
        OW::Plexies20260609::ResolveCoreRenderSkeletonMap(OW::eHero::HERO_CASSIDY);
    if (cassidyCore[0] != 18 || cassidyCore[1] != 16 || cassidyCore[2] != 15)
        return Fail();
    for (size_t index = 3; index < cassidyCore.size(); ++index) {
        if (cassidyCore[index] != OW::Plexies20260609::kUnusedRenderSkeletonBone)
            return Fail();
    }

    return EXIT_SUCCESS;
}
