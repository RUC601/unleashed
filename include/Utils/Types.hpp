#pragma once
#include <cmath>
#include <cstdint>
#include <DirectXMath.h>
#include <imgui.h>

namespace OW {

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#define M_RADPI        57.295779513082f
#define M_PI_F        ((float)(M_PI))
#define RAD2DEG( x  )  ( (float)(x) * (float)(180.f / M_PI_F) )
#define DEG2RAD( x  )  ( (float)(x) * (float)(M_PI_F / 180.f) )

class Vector2 {
public:
    float X = 0, Y = 0;
    Vector2() = default;
    Vector2(float x, float y) : X(x), Y(y) {}

    float Distance(Vector2 v) const { return sqrtf(powf(v.X - X, 2.0f) + powf(v.Y - Y, 2.0f)); }
    float Length() const { return sqrtf(powf(X, 2.0f) + powf(Y, 2.0f)); }
    Vector2 operator+(Vector2 v) const { return { X + v.X, Y + v.Y }; }
    Vector2 operator-(Vector2 v) const { return { X - v.X, Y - v.Y }; }
    Vector2 operator*(float s) const { return { X * s, Y * s }; }
    Vector2 operator/(float s) const { float rs = 1.f / s; return { X * rs, Y * rs }; }
    Vector2& operator+=(const Vector2& v) { X += v.X; Y += v.Y; return *this; }
    Vector2& operator-=(const Vector2& v) { X -= v.X; Y -= v.Y; return *this; }
    bool operator==(const Vector2& src) const { return src.X == X && src.Y == Y; }
    bool operator!=(const Vector2& src) const { return !(*this == src); }
};

class Vector3 {
public:
    float X = 0, Y = 0, Z = 0;
    Vector3() = default;
    Vector3(float x, float y, float z) : X(x), Y(y), Z(z) {}

    Vector3  operator-(const Vector3& V) const { return Vector3(X - V.X, Y - V.Y, Z - V.Z); }
    Vector3  operator+(const Vector3& V) const { return Vector3(X + V.X, Y + V.Y, Z + V.Z); }
    Vector3  operator*(float Scale)      const { return Vector3(X * Scale, Y * Scale, Z * Scale); }
    Vector3  operator/(float Scale)      const { float rs = 1.f / Scale; return Vector3(X * rs, Y * rs, Z * rs); }
    Vector3  operator+(float A)          const { return Vector3(X + A, Y + A, Z + A); }
    Vector3  operator-(float A)          const { return Vector3(X - A, Y - A, Z - A); }
    Vector3  operator*(const Vector3& V) const { return Vector3(X * V.X, Y * V.Y, Z * V.Z); }
    Vector3  operator/(const Vector3& V) const { return Vector3(X / V.X, Y / V.Y, Z / V.Z); }
    float    operator|(const Vector3& V) const { return X * V.X + Y * V.Y + Z * V.Z; }

    Vector3& operator+=(const Vector3& v) { X += v.X; Y += v.Y; Z += v.Z; return *this; }
    Vector3& operator-=(const Vector3& v) { X -= v.X; Y -= v.Y; Z -= v.Z; return *this; }
    Vector3& operator*=(const Vector3& v) { X *= v.X; Y *= v.Y; Z *= v.Z; return *this; }
    Vector3& operator/=(const Vector3& v) { X /= v.X; Y /= v.Y; Z /= v.Z; return *this; }

    bool operator==(const Vector3& src) const { return src.X == X && src.Y == Y && src.Z == Z; }
    bool operator!=(const Vector3& src) const { return !(*this == src); }

    Vector3 Rotate(float angle) const {
        return Vector3(X * cosf(-angle) - Z * sinf(-angle), Y, X * sinf(-angle) + Z * cosf(-angle));
    }
    float Size() const { return sqrtf(X * X + Y * Y + Z * Z); }
    float get_length() const {
        float ret = sqrtf(powf(X, 2.0f) + powf(Y, 2.0f) + powf(Z, 2.0f));
        return (ret > 0.0000000001f) ? ret : 0.0000000001f;
    }
    float DistTo(Vector3 targetTo) const { return (targetTo - *this).Size(); }
    Vector3 toRotator(Vector3 targetTo) const {
        Vector3 n = (targetTo - *this);
        return n * (1.f / n.Size());
    }
};

struct FRotator {
    double Pitch;  // radians, vertical (-PI/2..PI/2)
    double Yaw;    // radians, horizontal (-PI..PI)
    double Roll;   // radians, roll (-PI..PI)
};

inline Vector3 FrRotatorToVector3(const FRotator& rot) {
    return Vector3(
        static_cast<float>(rot.Pitch),
        static_cast<float>(rot.Yaw),
        static_cast<float>(rot.Roll)
    );
}

class Rect {
public:
    float x = 0, y = 0, width = 0, height = 0;
    Rect() = default;
    Rect(float _x, float _y, float w, float h) : x(_x), y(_y), width(w), height(h) {}
    bool operator==(const Rect& src) const { return src.x == x && src.y == y && src.width == width && src.height == height; }
    bool operator!=(const Rect& src) const { return !(*this == src); }
};

struct Color {
    uint8_t A = 255, R = 255, G = 255, B = 255;
    Color() = default;
    Color(int r, int g, int b, int a = 255) : R((uint8_t)r), G((uint8_t)g), B((uint8_t)b), A((uint8_t)a) {}
    Color(const DirectX::XMFLOAT3& Input)
        : R((uint8_t)(Input.x * 255.f)), G((uint8_t)(Input.y * 255.f)), B((uint8_t)(Input.z * 255.f)) {}

    unsigned int C2D()       const { return ((A & 0xff) << 24) | ((B & 0xff) << 16) | ((G & 0xff) << 8) | ((R & 0xff)); }
    unsigned int ApplyAlpha(int Alpha) const {
        return ((Alpha & 0xff) << 24) | ((R & 0xff)) | ((G & 0xff) << 8) | ((B & 0xff) << 16);
    }
    unsigned int RGBA2ARGB(int Alpha) const {
        return ((Alpha & 0xff) << 24) | ((B & 0xff)) | ((G & 0xff) << 8) | ((R & 0xff) << 16);
    }
};

class Matrix {
public:
    float m11=1,m12=0,m13=0,m14=0;
    float m21=0,m22=1,m23=0,m24=0;
    float m31=0,m32=0,m33=1,m34=0;
    float m41=0,m42=0,m43=0,m44=1;

    DirectX::XMFLOAT3 get_location() const;
    DirectX::XMFLOAT3 get_rotation() const;
    Vector3 GetCameraVec() const;
    bool WorldToScreen(const Vector3& worldPos, Vector2* OutPos, const Vector2& WindowSize, bool ignoreRet = false) const;
    // Convenience overload — returns Vector2 directly (uses WX/WY globals from Overwatch)
    Vector2 WorldToScreen(const Vector3& worldPos) const;
};

    // Helper for convertToHex with ImVec4 (used by Target.hpp)
    inline std::uint32_t ConvertImVec4ToHex(const ImVec4& color) {
        std::uint32_t r = static_cast<std::uint32_t>(color.x * 255.f);
        std::uint32_t g = static_cast<std::uint32_t>(color.y * 255.f);
        std::uint32_t b = static_cast<std::uint32_t>(color.z * 255.f);
        std::uint32_t a = static_cast<std::uint32_t>(color.w * 255.f);
        return ((a & 0xff) << 24) | ((b & 0xff) << 16) | ((g & 0xff) << 8) | (r & 0xff);
    }

} // namespace OW
