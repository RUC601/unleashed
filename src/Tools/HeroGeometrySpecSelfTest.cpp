#include "Game/BoneSlots.hpp"
#include "Game/Entity.hpp"
#include "Game/HeroGeometrySpec.hpp"
#include "Game/Structs.hpp"
#include "Utils/Config.hpp"

#include <array>
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
    if (!geometry || geometry->boneCount != 18)
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_HEAD))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_NECK))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_CHEST))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_BODY))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_BODY_BOT))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_PELVIS))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_L_HAND))
        return Fail();
    if (!OW::ResolveBoneHitboxSpec(0, OW::BONE_R_KNEE))
        return Fail();
    if (!OW::IsCoreHitboxBoneId(OW::BONE_HEAD) ||
        !OW::IsCoreHitboxBoneId(OW::BONE_NECK) ||
        !OW::IsCoreHitboxBoneId(OW::BONE_CHEST) ||
        OW::IsCoreHitboxBoneId(OW::BONE_PELVIS) ||
        OW::IsCoreHitboxBoneId(OW::BONE_R_HAND)) {
        return Fail();
    }
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_HEAD, 0.13f), 0.16f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_NECK, 0.13f), 0.14f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_CHEST, 0.13f), 0.22f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_PELVIS, 0.13f), 0.20f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_BODY, 0.13f), 0.22f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_L_HAND, 0.13f), 0.10f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, OW::BONE_R_KNEE, 0.13f), 0.12f))
        return Fail();
    if (!NearlyEqual(OW::ResolveBoneHitboxRadius(0, 123456, 0.13f), 0.13f))
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

    const auto cassidyRender =
        OW::Plexies20260609::ResolveRenderSkeletonMap(OW::eHero::HERO_CASSIDY);
    const std::array<int, 18> expectedCassidyRender{
        18, 16, 15, 2,
        13, 54, 14, 55,
        85, 95, 89, 99,
        28, 58, 90, 100,
        OW::Plexies20260609::kUnusedRenderSkeletonBone,
        OW::Plexies20260609::kUnusedRenderSkeletonBone,
    };
    if (cassidyRender != expectedCassidyRender)
        return Fail();
    for (size_t index = 0; index < OW::Plexies20260609::kSkeletonSlotCount; ++index) {
        if (cassidyRender[index] == OW::Plexies20260609::kUnusedRenderSkeletonBone)
            return Fail();
    }
    if (cassidyRender[16] != OW::Plexies20260609::kUnusedRenderSkeletonBone ||
        cassidyRender[17] != OW::Plexies20260609::kUnusedRenderSkeletonBone)
        return Fail();

    if (OW::Plexies20260609::HitboxBoneIdForRenderSlot(0) != OW::BONE_HEAD ||
        OW::Plexies20260609::HitboxBoneIdForRenderSlot(3) != OW::BONE_PELVIS ||
        OW::Plexies20260609::HitboxBoneIdForRenderSlot(12) != OW::BONE_L_HAND ||
        OW::Plexies20260609::HitboxBoneIdForRenderSlot(16) !=
            OW::Plexies20260609::kUnusedRenderSkeletonBone) {
        return Fail();
    }

    OW::c_entity partialSkeleton{};
    partialSkeleton.Alive = true;
    partialSkeleton.pos = OW::Vector3(10.0f, 20.0f, 30.0f);
    partialSkeleton.head_pos = OW::Vector3(10.0f, 21.8f, 30.0f);
    if (!partialSkeleton.FillMissingCoreAnchorsFromPosition())
        return Fail();
    if (!NearlyEqual(partialSkeleton.head_pos.Y, 21.8f) ||
        !NearlyEqual(partialSkeleton.neck_pos.Y, 21.35f) ||
        !NearlyEqual(partialSkeleton.chest_pos.Y, 21.05f)) {
        return Fail();
    }

    OW::c_entity completeSkeleton = partialSkeleton;
    if (completeSkeleton.FillMissingCoreAnchorsFromPosition())
        return Fail();

    OW::c_entity deadEntity{};
    deadEntity.Alive = false;
    deadEntity.pos = OW::Vector3(10.0f, 20.0f, 30.0f);
    if (deadEntity.FillMissingCoreAnchorsFromPosition() ||
        deadEntity.head_pos != OW::Vector3(0, 0, 0)) {
        return Fail();
    }

    OW::c_entity invalidPosition{};
    invalidPosition.Alive = true;
    invalidPosition.pos = OW::Vector3(0.0f, -1.0f, 0.0f);
    if (invalidPosition.FillMissingCoreAnchorsFromPosition())
        return Fail();

    return EXIT_SUCCESS;
}
