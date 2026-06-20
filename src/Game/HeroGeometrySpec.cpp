#include "Game/HeroGeometrySpec.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <DirectXMath.h>

#include "Game/Structs.hpp"
#include "Utils/Config.hpp"

namespace OW {
namespace {

using DirectX::XMFLOAT3;
using DirectX::XMMATRIX;
using DirectX::XMMatrixRotationY;
using DirectX::XMLoadFloat3;
using DirectX::XMStoreFloat3;
using DirectX::XMVector3Transform;

constexpr std::string_view kGeometryDataNote =
    "Runtime core geometry from aim_public_hero_geometry_0530.tsv; full skeleton IDs are retained separately as reference data.";

constexpr std::array<BoneHitboxSpec, 3> kFallbackBones = {
    BoneHitboxSpec{ BONE_HEAD, "head", 0.16f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_NECK, "neck", 0.14f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
    BoneHitboxSpec{ BONE_CHEST, "chest", 0.22f, Vector3{}, "sphere", "", kGeometryDataNote, 0.20f },
};

constexpr HeroGeometrySpec kFallbackGeometry{
    0,
    "fallback",
    kFallbackBones.data(),
    static_cast<int>(kFallbackBones.size())
};

bool IsZeroOffset(const Vector3& value)
{
    return value.X == 0.0f && value.Y == 0.0f && value.Z == 0.0f;
}

bool IsFiniteVector(const Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

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
                                float hitboxScalePercent,
                                float fallbackBoneRadius)
{
    const float boneRadius = ResolveBoneHitboxRadius(heroId, boneId, fallbackBoneRadius);
    const float projectileRadius = weapon ? (std::max)(0.0f, weapon->projectile.projectileRadius) : 0.0f;
    const float resolvedWindow = (std::max)(0.0f, boneRadius + projectileRadius);
    return resolvedWindow * Config::HitboxScaleMultiplier(hitboxScalePercent);
}

int AimBoneToSkeletonBoneId(int aimBone)
{
    switch (aimBone) {
    case Config::kAimBoneHead:
        return BONE_HEAD;
    case Config::kAimBoneNeck:
        return BONE_NECK;
    case Config::kAimBoneChest:
    case Config::kAimBoneClosest:
    default:
        return BONE_CHEST;
    }
}

bool IsCoreHitboxBoneId(int boneId)
{
    return boneId == BONE_HEAD || boneId == BONE_NECK || boneId == BONE_CHEST;
}

Vector3 ResolveBoneHitboxOffsetLocal(uint64_t heroId, int boneId)
{
    const BoneHitboxSpec* spec = ResolveBoneHitboxSpec(heroId, boneId);
    return spec ? spec->centerOffsetLocal : Vector3{};
}

Vector3 ResolveBoneHitboxCenter(const c_entity& entity, int boneId, const Vector3& bonePoint)
{
    const Vector3 offset = ResolveBoneHitboxOffsetLocal(entity.HeroID, boneId);
    if (IsZeroOffset(offset) || !IsFiniteVector(bonePoint))
        return bonePoint;

    XMFLOAT3 localOffset(offset.X, offset.Y, offset.Z);
    XMFLOAT3 rotatedOffset{};
    const XMMATRIX rotMatrix = XMMatrixRotationY(entity.Rot.X);
    XMStoreFloat3(
        &rotatedOffset,
        XMVector3Transform(XMLoadFloat3(&localOffset), rotMatrix));
    return bonePoint + Vector3(rotatedOffset.x, rotatedOffset.y, rotatedOffset.z);
}

} // namespace OW
