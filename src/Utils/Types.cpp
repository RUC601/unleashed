#include <Windows.h>
#include "Utils/Types.hpp"
#include "Game/Overwatch.hpp"

namespace OW {

DirectX::XMFLOAT3 Matrix::get_location() const
{
    __try {
        DirectX::XMMATRIX xm(
            m11, m12, m13, m14,
            m21, m22, m23, m24,
            m31, m32, m33, m34,
            m41, m42, m43, m44);
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(nullptr, xm);
        return DirectX::XMFLOAT3(
            DirectX::XMVectorGetX(invView.r[3]) / DirectX::XMVectorGetW(invView.r[3]),
            DirectX::XMVectorGetY(invView.r[3]) / DirectX::XMVectorGetW(invView.r[3]),
            DirectX::XMVectorGetZ(invView.r[3]) / DirectX::XMVectorGetW(invView.r[3])
        );
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return DirectX::XMFLOAT3(0, 0, 0);
}

DirectX::XMFLOAT3 Matrix::get_rotation() const
{
    __try {
        DirectX::XMMATRIX xm(
            m11, m12, m13, m14,
            m21, m22, m23, m24,
            m31, m32, m33, m34,
            m41, m42, m43, m44);
        return DirectX::XMFLOAT3(
            DirectX::XMVectorGetZ(xm.r[0]),
            DirectX::XMVectorGetZ(xm.r[1]),
            DirectX::XMVectorGetZ(xm.r[2])
        );
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return DirectX::XMFLOAT3(0, 0, 0);
}

Vector3 Matrix::GetCameraVec() const
{
    float A = m22*m33 - m32*m23,
          B = m32*m13 - m12*m33,
          C = m12*m23 - m22*m13,
          Z = m11*A + m21*B + m31*C;
    if (fabsf(Z) < 0.0001f) return Vector3();
    float D = m31*m23 - m21*m33,
          E = m11*m33 - m31*m13,
          F = m21*m13 - m11*m23,
          G = m21*m32 - m31*m22,
          H = m31*m12 - m11*m32,
          K = m11*m22 - m21*m12;
    return Vector3(
        -(A*m41 + D*m42 + G*m43) / Z,
        -(B*m41 + E*m42 + H*m43) / Z,
        -(C*m41 + F*m42 + K*m43) / Z
    );
}

bool Matrix::WorldToScreen(const Vector3& worldPos, Vector2* out, const Vector2& WindowSize, bool ignoreRet) const
{
    if (!out) return false;
    if (WindowSize.X <= 0.0f || WindowSize.Y <= 0.0f)
        return false;

    const float screenX = m11 * worldPos.X + m21 * worldPos.Y + m31 * worldPos.Z + m41;
    const float screenY = m12 * worldPos.X + m22 * worldPos.Y + m32 * worldPos.Z + m42;
    const float screenW = m14 * worldPos.X + m24 * worldPos.Y + m34 * worldPos.Z + m44;

    if (!ignoreRet && screenW < 0.001f) return false;
    if (std::fabs(screenW) < 0.001f) return false;

    const float ndcX = screenX / screenW;
    const float ndcY = screenY / screenW;
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
        return false;

    const float x = (ndcX + 1.0f) * 0.5f * WindowSize.X;
    const float y = (1.0f - ndcY) * 0.5f * WindowSize.Y;

    if (x < 0 || y < 0 || x >= WindowSize.X || y >= WindowSize.Y) return false;
    *out = Vector2(x, y);
    return true;
}

Vector2 Matrix::WorldToScreen(const Vector3& worldPos) const
{
    Vector2 result;
    WorldToScreen(worldPos, &result, Vector2(OW::WX, OW::WY));
    return result;
}

} // namespace OW
