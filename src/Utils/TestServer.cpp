#include "Utils/TestServer.hpp"

#ifdef UNLEASHED_TEST_SERVER

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Game/Decrypt.hpp"
#include "Game/GameData.hpp"
#include "Game/HeroPerks.hpp"
#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/ProcessConnection.hpp"

namespace TestServer {
namespace {

constexpr int kSchemaVersion = 1;
constexpr const char* kBindAddress = "127.0.0.1";
constexpr size_t kMaxRequestBytes = 8192;
constexpr size_t kTargetHistoryCapacity = 1024;
constexpr int kTargetHistoryPeriodMs = 8;

std::mutex g_lifecycleMutex;
std::thread g_serverThread;
std::thread g_targetHistoryThread;
std::atomic<bool> g_running{ false };
std::shared_ptr<std::atomic<bool>> g_runFlag;
SOCKET g_listenSocket = INVALID_SOCKET;
std::atomic<uint16_t> g_port{ 19550 };
std::atomic<bool> g_allowWildcardCors{ false };

struct RequestTarget {
    std::string path;
    std::unordered_map<std::string, std::string> query;
};

struct EntityQuery {
    float maxDistanceM = 50.0f;
    std::string team = "enemy";
    bool includeDead = false;
    bool teamDebug = false;
    int limit = 32;
    std::vector<std::string> warnings;
};

struct TargetHistorySample {
    uint64_t sequence = 0;
    uint64_t capturedAtMs = 0;
    std::string targetJson;
};

std::mutex g_targetHistoryMutex;
std::vector<TargetHistorySample> g_targetHistory;
uint64_t g_targetHistorySequence = 0;

uint64_t TimestampMs()
{
    const auto now = std::chrono::system_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}

std::string JsonEscape(const std::string& value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"':  out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20)
                out << "\\u" << std::setw(4) << static_cast<int>(ch);
            else
                out << static_cast<char>(ch);
            break;
        }
    }
    return out.str();
}

void AppendJsonString(std::ostringstream& out, const std::string& value)
{
    out << '"' << JsonEscape(value) << '"';
}

void AppendStringArray(std::ostringstream& out, const std::vector<std::string>& values)
{
    out << '[';
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0)
            out << ',';
        AppendJsonString(out, values[index]);
    }
    out << ']';
}

void AppendNumberOrNull(std::ostringstream& out, float value)
{
    if (!std::isfinite(value)) {
        out << "null";
        return;
    }
    out << std::setprecision(6) << value;
}

bool IsFiniteVector(const OW::Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

void AppendVectorOrNull(std::ostringstream& out, const OW::Vector3& value)
{
    if (!IsFiniteVector(value)) {
        out << "null";
        return;
    }

    out << "{\"x\":";
    AppendNumberOrNull(out, value.X);
    out << ",\"y\":";
    AppendNumberOrNull(out, value.Y);
    out << ",\"z\":";
    AppendNumberOrNull(out, value.Z);
    out << '}';
}

std::string HexString(uint64_t value)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

void AppendHexString(std::ostringstream& out, uint64_t value)
{
    AppendJsonString(out, HexString(value));
}

void AppendHexOrNull(std::ostringstream& out, uint64_t value)
{
    if (value == 0) {
        out << "null";
        return;
    }
    AppendHexString(out, value);
}

template <typename T>
bool TryReadValue(uint64_t address, T& value);

uint32_t ReadU32OrZero(uint64_t address)
{
    uint32_t value = 0;
    TryReadValue(address, value);
    return value;
}

void AppendTeamDebugJson(std::ostringstream& out, uint64_t teamBase)
{
    out << "{\"base\":";
    AppendHexOrNull(out, teamBase);
    const uint32_t flags58 = teamBase ? ReadU32OrZero(teamBase + OW::offset::Team_FlagsOffset) : 0;
    out << ",\"flags_58\":";
    AppendHexString(out, flags58);
    out << ",\"mask_58\":";
    AppendHexString(out, static_cast<uint64_t>(flags58 & OW::offset::Team_LegacyMask));
    out << ",\"comparison_key\":";
    AppendHexString(out, static_cast<uint64_t>(OW::offset::TeamComparisonKeyFromFlags(flags58)));
    out << ",\"raw_comparison_key\":";
    AppendHexString(out, static_cast<uint64_t>(OW::offset::TeamRawComparisonKeyFromFlags(flags58)));
    out << ",\"relation_code\":";
    AppendHexString(out, static_cast<uint64_t>(OW::offset::TeamRelationCodeFromFlags(flags58)));
    out << ",\"dwords\":[";
    for (uint64_t offset = 0x40; offset <= 0x80; offset += 4) {
        if (offset != 0x40)
            out << ',';
        out << "{\"offset\":";
        AppendHexString(out, offset);
        out << ",\"value\":";
        AppendHexString(out, teamBase ? ReadU32OrZero(teamBase + offset) : 0);
        out << '}';
    }
    out << "]}";
}

template <typename T>
bool TryReadValue(uint64_t address, T& value)
{
    value = {};
    return OW::SDK &&
        OW::SDK->IsInitialized() &&
        address != 0 &&
        OW::SDK->read_range(address, &value, sizeof(T));
}

struct ProjectionRawSample {
    float clipX = 0.0f;
    float clipY = 0.0f;
    float clipW = 0.0f;
    float ndcX = 0.0f;
    float ndcY = 0.0f;
    float screenX = 0.0f;
    float screenY = 0.0f;
    bool finite = false;
    bool wPositive = false;
    bool inBounds = false;
    bool ok = false;
};

ProjectionRawSample ProjectRaw(const OW::Matrix& matrix, const OW::Vector3& world, const OW::Vector2& window)
{
    ProjectionRawSample sample{};
    sample.clipX = matrix.m11 * world.X + matrix.m21 * world.Y + matrix.m31 * world.Z + matrix.m41;
    sample.clipY = matrix.m12 * world.X + matrix.m22 * world.Y + matrix.m32 * world.Z + matrix.m42;
    sample.clipW = matrix.m14 * world.X + matrix.m24 * world.Y + matrix.m34 * world.Z + matrix.m44;
    sample.wPositive = sample.clipW >= 0.001f;
    if (std::fabs(sample.clipW) >= 0.001f) {
        sample.ndcX = sample.clipX / sample.clipW;
        sample.ndcY = sample.clipY / sample.clipW;
        sample.screenX = (sample.ndcX + 1.0f) * 0.5f * window.X;
        sample.screenY = (1.0f - sample.ndcY) * 0.5f * window.Y;
    }
    sample.finite =
        std::isfinite(sample.clipX) &&
        std::isfinite(sample.clipY) &&
        std::isfinite(sample.clipW) &&
        std::isfinite(sample.ndcX) &&
        std::isfinite(sample.ndcY) &&
        std::isfinite(sample.screenX) &&
        std::isfinite(sample.screenY);
    sample.inBounds =
        sample.finite &&
        sample.screenX >= 0.0f &&
        sample.screenY >= 0.0f &&
        sample.screenX < window.X &&
        sample.screenY < window.Y;
    sample.ok = sample.wPositive && sample.inBounds;
    return sample;
}

void AppendProjectionRawSampleJson(std::ostringstream& out, const ProjectionRawSample& sample)
{
    out << "{\"ok\":" << (sample.ok ? "true" : "false")
        << ",\"finite\":" << (sample.finite ? "true" : "false")
        << ",\"w_positive\":" << (sample.wPositive ? "true" : "false")
        << ",\"in_bounds\":" << (sample.inBounds ? "true" : "false")
        << ",\"clip\":{\"x\":";
    AppendNumberOrNull(out, sample.clipX);
    out << ",\"y\":";
    AppendNumberOrNull(out, sample.clipY);
    out << ",\"w\":";
    AppendNumberOrNull(out, sample.clipW);
    out << "},\"ndc\":{\"x\":";
    AppendNumberOrNull(out, sample.ndcX);
    out << ",\"y\":";
    AppendNumberOrNull(out, sample.ndcY);
    out << "},\"screen\":{\"x\":";
    AppendNumberOrNull(out, sample.screenX);
    out << ",\"y\":";
    AppendNumberOrNull(out, sample.screenY);
    out << "}}";
}

void AppendMatrixProjectionJson(
    std::ostringstream& out,
    const char* name,
    bool matrixRead,
    bool matrixValid,
    const OW::Matrix& matrix,
    const OW::Vector3& world,
    const OW::Vector2& window)
{
    out << "{\"name\":";
    AppendJsonString(out, name ? name : "");
    out << ",\"matrix_read\":" << (matrixRead ? "true" : "false")
        << ",\"matrix_valid\":" << (matrixValid ? "true" : "false")
        << ",\"projection\":";
    if (matrixRead)
        AppendProjectionRawSampleJson(out, ProjectRaw(matrix, world, window));
    else
        out << "null";
    out << '}';
}

void AppendProjectionSetJson(
    std::ostringstream& out,
    const OW::Vector3& world,
    const OW::Vector2& window,
    const OW::Matrix& published,
    bool publishedValid,
    const OW::Matrix& directRender,
    bool directRead,
    bool directValid,
    const OW::Matrix& cameraProjection,
    bool cameraProjectionRead,
    bool cameraProjectionValid,
    const OW::Matrix& projectionCamera,
    bool projectionCameraRead,
    bool projectionCameraValid)
{
    out << '[';
    AppendMatrixProjectionJson(out, "published", true, publishedValid, published, world, window);
    out << ',';
    AppendMatrixProjectionJson(out, "direct_render_vp", directRead, directValid, directRender, world, window);
    out << ',';
    AppendMatrixProjectionJson(out, "camera_times_projection", cameraProjectionRead, cameraProjectionValid, cameraProjection, world, window);
    out << ',';
    AppendMatrixProjectionJson(out, "projection_times_camera", projectionCameraRead, projectionCameraValid, projectionCamera, world, window);
    out << ']';
}

float Dot(const OW::Vector3& lhs, const OW::Vector3& rhs)
{
    return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z;
}

bool MatrixNonIdentity(const OW::Matrix& matrix)
{
    const float values[] = {
        matrix.m11, matrix.m12, matrix.m13, matrix.m14,
        matrix.m21, matrix.m22, matrix.m23, matrix.m24,
        matrix.m31, matrix.m32, matrix.m33, matrix.m34,
        matrix.m41, matrix.m42, matrix.m43, matrix.m44
    };
    const float identity[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    bool differsFromIdentity = false;
    bool hasNonZeroValue = false;
    for (size_t index = 0; index < 16; ++index) {
        if (!std::isfinite(values[index]))
            return false;
        if (std::fabs(values[index]) > 0.0001f)
            hasNonZeroValue = true;
        if (std::fabs(values[index] - identity[index]) > 0.001f)
            differsFromIdentity = true;
    }
    return hasNonZeroValue && differsFromIdentity;
}

bool CameraViewMatrixPlausible(const OW::Matrix& matrix)
{
    if (!MatrixNonIdentity(matrix))
        return false;
    const DirectX::XMFLOAT3 camera = matrix.get_location();
    const DirectX::XMFLOAT3 forward = matrix.get_rotation();
    const float cameraLengthSq = camera.x * camera.x + camera.y * camera.y + camera.z * camera.z;
    const float forwardLengthSq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
    return std::isfinite(camera.x) && std::isfinite(camera.y) && std::isfinite(camera.z) &&
        std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
        cameraLengthSq > 0.000001f && cameraLengthSq < 1000000000000.0f &&
        forwardLengthSq > 0.0625f && forwardLengthSq < 4.0f;
}

OW::Matrix MultiplyMatricesLocal(const OW::Matrix& a, const OW::Matrix& b)
{
    OW::Matrix result{};
    result.m11 = a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31 + a.m14 * b.m41;
    result.m12 = a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32 + a.m14 * b.m42;
    result.m13 = a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33 + a.m14 * b.m43;
    result.m14 = a.m11 * b.m14 + a.m12 * b.m24 + a.m13 * b.m34 + a.m14 * b.m44;
    result.m21 = a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31 + a.m24 * b.m41;
    result.m22 = a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32 + a.m24 * b.m42;
    result.m23 = a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33 + a.m24 * b.m43;
    result.m24 = a.m21 * b.m14 + a.m22 * b.m24 + a.m23 * b.m34 + a.m24 * b.m44;
    result.m31 = a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31 + a.m34 * b.m41;
    result.m32 = a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32 + a.m34 * b.m42;
    result.m33 = a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33 + a.m34 * b.m43;
    result.m34 = a.m31 * b.m14 + a.m32 * b.m24 + a.m33 * b.m34 + a.m34 * b.m44;
    result.m41 = a.m41 * b.m11 + a.m42 * b.m21 + a.m43 * b.m31 + a.m44 * b.m41;
    result.m42 = a.m41 * b.m12 + a.m42 * b.m22 + a.m43 * b.m32 + a.m44 * b.m42;
    result.m43 = a.m41 * b.m13 + a.m42 * b.m23 + a.m43 * b.m33 + a.m44 * b.m43;
    result.m44 = a.m41 * b.m14 + a.m42 * b.m24 + a.m43 * b.m34 + a.m44 * b.m44;
    return result;
}

std::string QwordAsciiPreview(uint64_t value)
{
    char text[9] = {};
    for (int index = 0; index < 8; ++index) {
        const unsigned char ch =
            static_cast<unsigned char>((value >> (index * 8)) & 0xFFu);
        text[index] = std::isprint(ch) ? static_cast<char>(ch) : '.';
    }
    return std::string(text, 8);
}

bool ProbeMatrixNonIdentity(const OW::Matrix& matrix)
{
    const float values[] = {
        matrix.m11, matrix.m12, matrix.m13, matrix.m14,
        matrix.m21, matrix.m22, matrix.m23, matrix.m24,
        matrix.m31, matrix.m32, matrix.m33, matrix.m34,
        matrix.m41, matrix.m42, matrix.m43, matrix.m44
    };
    const float identity[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    bool differsFromIdentity = false;
    bool hasNonZeroValue = false;
    for (size_t index = 0; index < 16; ++index) {
        if (!std::isfinite(values[index]))
            return false;
        if (std::fabs(values[index]) > 0.0001f)
            hasNonZeroValue = true;
        if (std::fabs(values[index] - identity[index]) > 0.001f)
            differsFromIdentity = true;
    }
    return hasNonZeroValue && differsFromIdentity;
}

bool ProbeCameraViewMatrixPlausible(const OW::Matrix& matrix)
{
    if (!ProbeMatrixNonIdentity(matrix))
        return false;

    const DirectX::XMFLOAT3 camera = matrix.get_location();
    const DirectX::XMFLOAT3 forward = matrix.get_rotation();
    const float cameraLengthSq =
        camera.x * camera.x + camera.y * camera.y + camera.z * camera.z;
    const float forwardLengthSq =
        forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;

    return std::isfinite(camera.x) && std::isfinite(camera.y) &&
        std::isfinite(camera.z) && std::isfinite(forward.x) &&
        std::isfinite(forward.y) && std::isfinite(forward.z) &&
        cameraLengthSq > 0.000001f && cameraLengthSq < 1000000000000.0f &&
        forwardLengthSq > 0.0625f && forwardLengthSq < 4.0f;
}

OW::Matrix ProbeMultiplyMatrices(const OW::Matrix& a, const OW::Matrix& b)
{
    OW::Matrix result{};
    result.m11 = a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31 + a.m14 * b.m41;
    result.m12 = a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32 + a.m14 * b.m42;
    result.m13 = a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33 + a.m14 * b.m43;
    result.m14 = a.m11 * b.m14 + a.m12 * b.m24 + a.m13 * b.m34 + a.m14 * b.m44;
    result.m21 = a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31 + a.m24 * b.m41;
    result.m22 = a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32 + a.m24 * b.m42;
    result.m23 = a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33 + a.m24 * b.m43;
    result.m24 = a.m21 * b.m14 + a.m22 * b.m24 + a.m23 * b.m34 + a.m24 * b.m44;
    result.m31 = a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31 + a.m34 * b.m41;
    result.m32 = a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32 + a.m34 * b.m42;
    result.m33 = a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33 + a.m34 * b.m43;
    result.m34 = a.m31 * b.m14 + a.m32 * b.m24 + a.m33 * b.m34 + a.m34 * b.m44;
    result.m41 = a.m41 * b.m11 + a.m42 * b.m21 + a.m43 * b.m31 + a.m44 * b.m41;
    result.m42 = a.m41 * b.m12 + a.m42 * b.m22 + a.m43 * b.m32 + a.m44 * b.m42;
    result.m43 = a.m41 * b.m13 + a.m42 * b.m23 + a.m43 * b.m33 + a.m44 * b.m43;
    result.m44 = a.m41 * b.m14 + a.m42 * b.m24 + a.m43 * b.m34 + a.m44 * b.m44;
    return result;
}

float ProjectionScaleToFovDeg(float scale)
{
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    const float absScale = std::fabs(scale);
    if (!std::isfinite(absScale) || absScale <= 0.000001f)
        return (std::numeric_limits<float>::quiet_NaN)();
    return kRadToDeg * 2.0f * std::atan(1.0f / absScale);
}

void AppendProjectionFovJson(std::ostringstream& out,
                             const OW::Matrix& projection,
                             bool projectionRead,
                             const OW::Vector2& window)
{
    const float aspect = window.Y > 0.0f ? window.X / window.Y : 0.0f;
    out << "{\"projection_read\":" << (projectionRead ? "true" : "false")
        << ",\"aspect\":";
    AppendNumberOrNull(out, aspect);
    out << ",\"m11\":";
    AppendNumberOrNull(out, projection.m11);
    out << ",\"m22\":";
    AppendNumberOrNull(out, projection.m22);
    out << ",\"m14\":";
    AppendNumberOrNull(out, projection.m14);
    out << ",\"m24\":";
    AppendNumberOrNull(out, projection.m24);
    out << ",\"m34\":";
    AppendNumberOrNull(out, projection.m34);
    out << ",\"m43\":";
    AppendNumberOrNull(out, projection.m43);
    out << ",\"m44\":";
    AppendNumberOrNull(out, projection.m44);
    out << ",\"composition_m14_zeroed\":"
        << (projectionRead && OW::ProjectionMatrixNeedsCompositionM14Zero(projection) ? "true" : "false");
    out << ",\"horizontal_deg_from_m11\":";
    AppendNumberOrNull(out, projectionRead ? ProjectionScaleToFovDeg(projection.m11) : (std::numeric_limits<float>::quiet_NaN)());
    out << ",\"vertical_deg_from_m22\":";
    AppendNumberOrNull(out, projectionRead ? ProjectionScaleToFovDeg(projection.m22) : (std::numeric_limits<float>::quiet_NaN)());
    out << ",\"horizontal_deg_from_m22_aspect\":";
    AppendNumberOrNull(out,
        projectionRead && aspect > 0.0f && std::fabs(projection.m22) > 0.000001f
            ? ProjectionScaleToFovDeg(projection.m22 / aspect)
            : (std::numeric_limits<float>::quiet_NaN)());
    out << ",\"vertical_deg_from_m11_aspect\":";
    AppendNumberOrNull(out,
        projectionRead && aspect > 0.0f && std::fabs(projection.m11) > 0.000001f
            ? ProjectionScaleToFovDeg(projection.m11 * aspect)
            : (std::numeric_limits<float>::quiet_NaN)());
    out << ",\"default_horizontal_deg\":";
    AppendNumberOrNull(out, OW::offset::Default_FOV_Horizontal_Deg);
    out << ",\"configured_aim_fov_deg\":";
    AppendNumberOrNull(out, OW::Config::Fov);
    out << ",\"runtime_draw_fov_deg\":";
    AppendNumberOrNull(out, OW::Config::RuntimeDrawFovOrDefault(OW::Config::Fov));
    out << '}';
}

void AppendMatrixProbeJson(std::ostringstream& out, uint64_t address)
{
    OW::Matrix matrix{};
    const bool read = TryReadValue(address, matrix);
    out << "{\"address\":";
    AppendHexOrNull(out, address);
    out << ",\"read\":" << (read ? "true" : "false")
        << ",\"non_identity\":"
        << (read && ProbeMatrixNonIdentity(matrix) ? "true" : "false")
        << ",\"camera_view_plausible\":"
        << (read && ProbeCameraViewMatrixPlausible(matrix) ? "true" : "false");
    if (read) {
        out << ",\"m11\":";
        AppendNumberOrNull(out, matrix.m11);
        out << ",\"m22\":";
        AppendNumberOrNull(out, matrix.m22);
        out << ",\"m33\":";
        AppendNumberOrNull(out, matrix.m33);
        out << ",\"m44\":";
        AppendNumberOrNull(out, matrix.m44);
        out << ",\"m14\":";
        AppendNumberOrNull(out, matrix.m14);
        out << ",\"m24\":";
        AppendNumberOrNull(out, matrix.m24);
        out << ",\"m34\":";
        AppendNumberOrNull(out, matrix.m34);
    }
    out << '}';
}

void AppendRvaQwordProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t base,
    uint64_t rva)
{
    uint64_t value = 0;
    const uint64_t address = base ? base + rva : 0;
    const bool read = rva != 0 && TryReadValue(address, value);
    const bool plausible = read && OW::IsPlausibleUserPointer(value);
    uint64_t pointedQword = 0;
    const bool pointedRead = plausible && TryReadValue(value, pointedQword);

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"rva\":";
    AppendHexString(out, rva);
    out << ",\"address\":";
    AppendHexOrNull(out, address);
    out << ",\"read\":" << (read ? "true" : "false")
        << ",\"u64\":";
    AppendHexOrNull(out, value);
    out << ",\"ascii\":";
    AppendJsonString(out, read ? QwordAsciiPreview(value) : "");
    out << ",\"plausible_pointer\":" << (plausible ? "true" : "false")
        << ",\"pointed_qword_read\":" << (pointedRead ? "true" : "false")
        << ",\"pointed_qword\":";
    AppendHexOrNull(out, pointedQword);
    out << '}';
}

void AppendRvaDwordProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t base,
    uint64_t rva)
{
    uint32_t value = 0;
    const uint64_t address = base ? base + rva : 0;
    const bool read = rva != 0 && TryReadValue(address, value);
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"rva\":";
    AppendHexString(out, rva);
    out << ",\"address\":";
    AppendHexOrNull(out, address);
    out << ",\"read\":" << (read ? "true" : "false")
        << ",\"u32\":";
    AppendHexString(out, value);
    out << ",\"decimal\":" << value << '}';
}

void AppendComponentMaterialProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t base,
    uint64_t sourceRva,
    uint64_t qwordOffset,
    uint64_t byteRva)
{
    uint64_t source = 0;
    uint64_t material = 0;
    uint8_t byte = 0;
    const bool sourceRead = TryReadValue(base + sourceRva, source);
    const bool materialRead =
        OW::IsPlausibleUserPointer(source) &&
        TryReadValue(source + qwordOffset, material);
    const bool byteRead = TryReadValue(base + byteRva, byte);

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"source_rva\":";
    AppendHexString(out, sourceRva);
    out << ",\"source_read\":" << (sourceRead ? "true" : "false")
        << ",\"source\":";
    AppendHexOrNull(out, source);
    out << ",\"source_plausible\":"
        << (OW::IsPlausibleUserPointer(source) ? "true" : "false")
        << ",\"qword_offset\":";
    AppendHexString(out, qwordOffset);
    out << ",\"material_read\":" << (materialRead ? "true" : "false")
        << ",\"material\":";
    AppendHexOrNull(out, material);
    out << ",\"byte_rva\":";
    AppendHexString(out, byteRva);
    out << ",\"byte_read\":" << (byteRead ? "true" : "false")
        << ",\"byte\":";
    AppendHexString(out, byte);
    out << '}';
}

void AppendFloatScanJson(
    std::ostringstream& out,
    const char* name,
    uint64_t address,
    size_t scanBytes,
    size_t maxResults)
{
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"base\":";
    AppendHexOrNull(out, address);
    out << ",\"scan_bytes\":";
    AppendHexString(out, static_cast<uint64_t>(scanBytes));
    out << ",\"candidates\":[";

    size_t emitted = 0;
    for (size_t offset = 0; offset + sizeof(float) <= scanBytes; offset += sizeof(float)) {
        float value = 0.0f;
        if (!TryReadValue(address + offset, value))
            continue;
        if (!std::isfinite(value) || value <= 0.0f || value >= 100.0f)
            continue;

        if (emitted != 0)
            out << ',';
        out << "{\"offset\":";
        AppendHexString(out, static_cast<uint64_t>(offset));
        out << ",\"address\":";
        AppendHexOrNull(out, address + offset);
        out << ",\"value\":";
        AppendNumberOrNull(out, value);
        out << ",\"near_config_sensitivity\":"
            << (std::fabs(value - OW::Config::gameMouseSensitivity) <= 0.001f
                    ? "true"
                    : "false")
            << '}';

        ++emitted;
        if (emitted >= maxResults)
            break;
    }

    out << "]}";
}

struct TargetFloatHit {
    uint64_t offset = 0;
    uint64_t address = 0;
    float value = 0.0f;
};

std::vector<TargetFloatHit> CollectFloatTargetHits(
    uint64_t address,
    size_t scanBytes,
    float target,
    float tolerance,
    size_t maxHits)
{
    constexpr size_t kChunkSize = 0x1000;
    std::vector<TargetFloatHit> hits;
    if (!address || !OW::SDK || maxHits == 0)
        return hits;

    std::vector<uint8_t> buffer(kChunkSize);
    for (size_t chunkOffset = 0; chunkOffset < scanBytes; chunkOffset += kChunkSize) {
        const size_t remaining = scanBytes - chunkOffset;
        const size_t chunkSize = remaining < kChunkSize ? remaining : kChunkSize;
        if (!OW::SDK->read_range(address + chunkOffset, buffer.data(), chunkSize))
            continue;

        for (size_t offset = 0; offset + sizeof(float) <= chunkSize; offset += sizeof(float)) {
            float value = 0.0f;
            std::memcpy(&value, buffer.data() + offset, sizeof(value));
            if (!std::isfinite(value) || std::fabs(value - target) > tolerance)
                continue;

            TargetFloatHit hit{};
            hit.offset = static_cast<uint64_t>(chunkOffset + offset);
            hit.address = address + hit.offset;
            hit.value = value;
            hits.push_back(hit);
            if (hits.size() >= maxHits)
                return hits;
        }
    }

    return hits;
}

void AppendFloatTargetScanJson(
    std::ostringstream& out,
    const char* name,
    uint64_t address,
    size_t scanBytes,
    float target,
    float tolerance,
    size_t maxHits)
{
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"base\":";
    AppendHexOrNull(out, address);
    out << ",\"scan_bytes\":";
    AppendHexString(out, static_cast<uint64_t>(scanBytes));
    out << ",\"target\":";
    AppendNumberOrNull(out, target);
    out << ",\"tolerance\":";
    AppendNumberOrNull(out, tolerance);
    out << ",\"hits\":[";

    const auto hits =
        CollectFloatTargetHits(address, scanBytes, target, tolerance, maxHits);
    for (size_t index = 0; index < hits.size(); ++index) {
        if (index != 0)
            out << ',';
        out << "{\"offset\":";
        AppendHexString(out, hits[index].offset);
        out << ",\"address\":";
        AppendHexOrNull(out, hits[index].address);
        out << ",\"value\":";
        AppendNumberOrNull(out, hits[index].value);
        out << '}';
    }

    out << "]}";
}

void AppendFloatAtJson(std::ostringstream& out, const char* name, uint64_t address)
{
    uint32_t raw = 0;
    float value = 0.0f;
    const bool read = TryReadValue(address, raw);
    if (read)
        std::memcpy(&value, &raw, sizeof(value));

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"address\":";
    AppendHexOrNull(out, address);
    out << ",\"read\":" << (read ? "true" : "false")
        << ",\"raw\":";
    AppendHexString(out, raw);
    out << ",\"finite\":" << (read && std::isfinite(value) ? "true" : "false")
        << ",\"plausible_sensitivity\":"
        << (read && OW::IsPlausibleSensitivity(value) ? "true" : "false")
        << ",\"value\":";
    if (read && std::isfinite(value))
        AppendNumberOrNull(out, value);
    else
        out << "null";
    out << '}';
}

void AppendNearbyFloatsJson(
    std::ostringstream& out,
    uint64_t center,
    uint64_t base,
    int64_t before,
    int64_t after)
{
    out << '[';
    bool emitted = false;
    for (int64_t delta = -before; delta <= after; delta += 4) {
        const uint64_t address = static_cast<uint64_t>(
            static_cast<int64_t>(center) + delta);
        uint32_t raw = 0;
        float value = 0.0f;
        const bool read = TryReadValue(address, raw);
        if (read)
            std::memcpy(&value, &raw, sizeof(value));
        if (!read || !std::isfinite(value))
            continue;

        if (emitted)
            out << ',';
        emitted = true;
        out << "{\"delta\":";
        AppendHexString(out, static_cast<uint64_t>(delta));
        out << ",\"root_offset\":";
        AppendHexString(out, address - base);
        out << ",\"address\":";
        AppendHexOrNull(out, address);
        out << ",\"raw\":";
        AppendHexString(out, raw);
        out << ",\"value\":";
        AppendNumberOrNull(out, value);
        out << ",\"plausible_sensitivity\":"
            << (OW::IsPlausibleSensitivity(value) ? "true" : "false")
            << '}';
    }
    out << ']';
}

void AppendSensitivityObjectCandidateJson(
    std::ostringstream& out,
    const char* name,
    uint64_t object,
    uint64_t hitAddress)
{
    uint8_t invertY = 0;
    const bool invertYRead =
        OW::IsPlausibleUserPointer(object) &&
        TryReadValue(object + OW::offset::Invert_Y_Flag, invertY);

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"object\":";
    AppendHexOrNull(out, object);
    out << ",\"plausible_pointer\":"
        << (OW::IsPlausibleUserPointer(object) ? "true" : "false")
        << ",\"hit_delta\":";
    AppendHexString(out, hitAddress >= object ? hitAddress - object : 0);
    out << ",\"invert_y\":{\"read\":"
        << (invertYRead ? "true" : "false")
        << ",\"value\":" << static_cast<unsigned int>(invertY)
        << "},\"fields\":[";
    AppendFloatAtJson(out, "sens_x_2224", object + OW::offset::SensX_Scale);
    out << ',';
    AppendFloatAtJson(out, "sens_y_2228", object + OW::offset::SensY_Scale);
    out << ',';
    AppendFloatAtJson(out, "sensitivity_2238", object + OW::offset::Sensitivity);
    out << ',';
    AppendFloatAtJson(out, "hit_plus_0", hitAddress);
    out << ',';
    AppendFloatAtJson(out, "hit_plus_4", hitAddress + 4);
    out << "],\"pair_xy_match\":";

    float sensX = 0.0f;
    float sensY = 0.0f;
    const bool sensXRead =
        TryReadValue(object + OW::offset::SensX_Scale, sensX);
    const bool sensYRead =
        TryReadValue(object + OW::offset::SensY_Scale, sensY);
    out << (sensXRead && sensYRead &&
                    OW::IsPlausibleSensitivity(sensX) &&
                    OW::IsPlausibleSensitivity(sensY) &&
                    std::fabs(sensX - sensY) <= 0.01f
                ? "true"
                : "false")
        << '}';
}

void AppendPointerRefsContainingJson(
    std::ostringstream& out,
    const char* name,
    uint64_t root,
    size_t scanBytes,
    uint64_t hitAddress,
    uint64_t maxDelta,
    size_t maxRefs)
{
    constexpr size_t kChunkSize = 0x1000;
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"root\":";
    AppendHexOrNull(out, root);
    out << ",\"scan_bytes\":";
    AppendHexString(out, static_cast<uint64_t>(scanBytes));
    out << ",\"max_delta\":";
    AppendHexString(out, maxDelta);
    out << ",\"refs\":[";

    size_t emitted = 0;
    std::vector<uint8_t> buffer(kChunkSize);
    for (size_t chunkOffset = 0;
         root && chunkOffset < scanBytes && emitted < maxRefs;
         chunkOffset += kChunkSize) {
        const size_t remaining = scanBytes - chunkOffset;
        const size_t chunkSize = remaining < kChunkSize ? remaining : kChunkSize;
        if (!OW::SDK || !OW::SDK->read_range(root + chunkOffset, buffer.data(), chunkSize))
            continue;

        for (size_t offset = 0;
             offset + sizeof(uint64_t) <= chunkSize && emitted < maxRefs;
             offset += sizeof(uint64_t)) {
            uint64_t pointer = 0;
            std::memcpy(&pointer, buffer.data() + offset, sizeof(pointer));
            if (!OW::IsPlausibleUserPointer(pointer) ||
                pointer > hitAddress ||
                hitAddress - pointer > maxDelta) {
                continue;
            }

            if (emitted != 0)
                out << ',';
            const uint64_t absoluteOffset =
                static_cast<uint64_t>(chunkOffset + offset);
            out << "{\"field_offset\":";
            AppendHexString(out, absoluteOffset);
            out << ",\"field_address\":";
            AppendHexOrNull(out, root + absoluteOffset);
            out << ",\"pointer\":";
            AppendHexOrNull(out, pointer);
            out << ",\"hit_delta\":";
            AppendHexString(out, hitAddress - pointer);
            out << '}';
            ++emitted;
        }
    }

    out << "]}";
}

void AppendRangeRefsContainingJson(
    std::ostringstream& out,
    const char* name,
    uint64_t root,
    size_t scanBytes,
    uint64_t hitAddress,
    size_t maxRefs)
{
    constexpr size_t kChunkSize = 0x1000;
    constexpr uint64_t kMaxVectorSpan = 0x100000;
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"root\":";
    AppendHexOrNull(out, root);
    out << ",\"scan_bytes\":";
    AppendHexString(out, static_cast<uint64_t>(scanBytes));
    out << ",\"refs\":[";

    size_t emitted = 0;
    std::vector<uint8_t> buffer(kChunkSize);
    for (size_t chunkOffset = 0;
         root && chunkOffset < scanBytes && emitted < maxRefs;
         chunkOffset += kChunkSize) {
        const size_t remaining = scanBytes - chunkOffset;
        const size_t chunkSize = remaining < kChunkSize ? remaining : kChunkSize;
        if (!OW::SDK || !OW::SDK->read_range(root + chunkOffset, buffer.data(), chunkSize))
            continue;

        for (size_t offset = 0;
             offset + 3 * sizeof(uint64_t) <= chunkSize && emitted < maxRefs;
             offset += sizeof(uint64_t)) {
            uint64_t begin = 0;
            uint64_t end = 0;
            uint64_t capacity = 0;
            std::memcpy(&begin, buffer.data() + offset, sizeof(begin));
            std::memcpy(&end, buffer.data() + offset + 8, sizeof(end));
            std::memcpy(&capacity, buffer.data() + offset + 16, sizeof(capacity));
            if (!OW::IsPlausibleUserPointer(begin) ||
                !OW::IsPlausibleUserPointer(end) ||
                !OW::IsPlausibleUserPointer(capacity) ||
                begin > end ||
                end > capacity ||
                capacity - begin > kMaxVectorSpan ||
                hitAddress < begin ||
                hitAddress >= capacity) {
                continue;
            }

            if (emitted != 0)
                out << ',';
            const uint64_t absoluteOffset =
                static_cast<uint64_t>(chunkOffset + offset);
            out << "{\"field_offset\":";
            AppendHexString(out, absoluteOffset);
            out << ",\"field_address\":";
            AppendHexOrNull(out, root + absoluteOffset);
            out << ",\"begin\":";
            AppendHexOrNull(out, begin);
            out << ",\"end\":";
            AppendHexOrNull(out, end);
            out << ",\"capacity\":";
            AppendHexOrNull(out, capacity);
            out << ",\"hit_delta\":";
            AppendHexString(out, hitAddress - begin);
            out << '}';
            ++emitted;
        }
    }

    out << "]}";
}

void AppendPointerRefsNearTargetJson(
    std::ostringstream& out,
    const char* name,
    uint64_t root,
    size_t scanBytes,
    uint64_t target,
    uint64_t window,
    size_t maxRefs)
{
    constexpr size_t kChunkSize = 0x1000;
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"root\":";
    AppendHexOrNull(out, root);
    out << ",\"scan_bytes\":";
    AppendHexString(out, static_cast<uint64_t>(scanBytes));
    out << ",\"target\":";
    AppendHexOrNull(out, target);
    out << ",\"window\":";
    AppendHexString(out, window);
    out << ",\"refs\":[";

    size_t emitted = 0;
    std::vector<uint8_t> buffer(kChunkSize);
    const uint64_t low = target > window ? target - window : 0;
    const uint64_t high = target + window;
    for (size_t chunkOffset = 0;
         root && target && chunkOffset < scanBytes && emitted < maxRefs;
         chunkOffset += kChunkSize) {
        const size_t remaining = scanBytes - chunkOffset;
        const size_t chunkSize = remaining < kChunkSize ? remaining : kChunkSize;
        if (!OW::SDK || !OW::SDK->read_range(root + chunkOffset, buffer.data(), chunkSize))
            continue;

        for (size_t offset = 0;
             offset + sizeof(uint64_t) <= chunkSize && emitted < maxRefs;
             offset += sizeof(uint64_t)) {
            uint64_t pointer = 0;
            std::memcpy(&pointer, buffer.data() + offset, sizeof(pointer));
            if (!OW::IsPlausibleUserPointer(pointer) ||
                pointer < low ||
                pointer > high) {
                continue;
            }

            if (emitted != 0)
                out << ',';
            const uint64_t absoluteOffset =
                static_cast<uint64_t>(chunkOffset + offset);
            const int64_t delta =
                static_cast<int64_t>(pointer) - static_cast<int64_t>(target);
            out << "{\"field_offset\":";
            AppendHexString(out, absoluteOffset);
            out << ",\"field_address\":";
            AppendHexOrNull(out, root + absoluteOffset);
            out << ",\"pointer\":";
            AppendHexOrNull(out, pointer);
            out << ",\"delta\":" << delta
                << '}';
            ++emitted;
        }
    }

    out << "]}";
}

void AppendSensitivityCandidatePointerRefsJson(
    std::ostringstream& out,
    const char* candidateName,
    uint64_t object,
    uint64_t liveClientGame,
    uint64_t liveGameAdmin,
    uint64_t forumGlobalAdmin)
{
    out << "{\"candidate\":";
    AppendJsonString(out, candidateName);
    out << ",\"object\":";
    AppendHexOrNull(out, object);
    out << ",\"refs\":[";
    AppendPointerRefsNearTargetJson(
        out,
        "client_game",
        liveClientGame,
        0x10000,
        object,
        0x80,
        16);
    out << ',';
    AppendPointerRefsNearTargetJson(
        out,
        "live_game_admin",
        liveGameAdmin,
        0x40000,
        object,
        0x80,
        16);
    out << ',';
    AppendPointerRefsNearTargetJson(
        out,
        "forum_global_admin",
        forumGlobalAdmin,
        0x10000,
        object,
        0x80,
        16);
    out << "]}";
}

void AppendBzSensitivityStructureJson(
    std::ostringstream& out,
    uint64_t liveClientGame,
    uint64_t liveGameAdmin,
    uint64_t forumGlobalAdmin)
{
    constexpr float kTargetSensitivity = 4.5f;
    constexpr float kTolerance = 0.002f;
    constexpr size_t kScanBytes = 0x40000;
    const auto hits = CollectFloatTargetHits(
        liveGameAdmin,
        kScanBytes,
        kTargetSensitivity,
        kTolerance,
        16);

    out << "{\"target\":";
    AppendNumberOrNull(out, kTargetSensitivity);
    out << ",\"tolerance\":";
    AppendNumberOrNull(out, kTolerance);
    out << ",\"live_game_admin\":";
    AppendHexOrNull(out, liveGameAdmin);
    out << ",\"hits\":[";

    for (size_t index = 0; index < hits.size(); ++index) {
        if (index != 0)
            out << ',';
        const auto& hit = hits[index];
        out << "{\"offset\":";
        AppendHexString(out, hit.offset);
        out << ",\"address\":";
        AppendHexOrNull(out, hit.address);
        out << ",\"value\":";
        AppendNumberOrNull(out, hit.value);
        out << ",\"nearby_floats\":";
        AppendNearbyFloatsJson(out, hit.address, liveGameAdmin, 0x30, 0x30);
        out << ",\"object_candidates\":[";
        AppendSensitivityObjectCandidateJson(
            out,
            "if_hit_is_sens_x_2224",
            hit.address - OW::offset::SensX_Scale,
            hit.address);
        out << ',';
        AppendSensitivityObjectCandidateJson(
            out,
            "if_hit_is_sens_y_2228",
            hit.address - OW::offset::SensY_Scale,
            hit.address);
        out << ',';
        AppendSensitivityObjectCandidateJson(
            out,
            "if_hit_is_sensitivity_2238",
            hit.address - OW::offset::Sensitivity,
            hit.address);
        out << "],\"candidate_pointer_refs\":[";
        AppendSensitivityCandidatePointerRefsJson(
            out,
            "if_hit_is_sens_x_2224",
            hit.address - OW::offset::SensX_Scale,
            liveClientGame,
            liveGameAdmin,
            forumGlobalAdmin);
        out << ',';
        AppendSensitivityCandidatePointerRefsJson(
            out,
            "if_hit_is_sens_y_2228",
            hit.address - OW::offset::SensY_Scale,
            liveClientGame,
            liveGameAdmin,
            forumGlobalAdmin);
        out << ',';
        AppendSensitivityCandidatePointerRefsJson(
            out,
            "if_hit_is_sensitivity_2238",
            hit.address - OW::offset::Sensitivity,
            liveClientGame,
            liveGameAdmin,
            forumGlobalAdmin);
        out << "],\"pointer_refs\":[";
        AppendPointerRefsContainingJson(
            out,
            "client_game",
            liveClientGame,
            0x10000,
            hit.address,
            0x40000,
            24);
        out << ',';
        AppendPointerRefsContainingJson(
            out,
            "live_game_admin",
            liveGameAdmin,
            kScanBytes,
            hit.address,
            0x40000,
            24);
        out << ',';
        AppendPointerRefsContainingJson(
            out,
            "forum_global_admin",
            forumGlobalAdmin,
            0x10000,
            hit.address,
            0x40000,
            24);
        out << "],\"range_refs\":[";
        AppendRangeRefsContainingJson(
            out,
            "client_game",
            liveClientGame,
            0x10000,
            hit.address,
            12);
        out << ',';
        AppendRangeRefsContainingJson(
            out,
            "live_game_admin",
            liveGameAdmin,
            kScanBytes,
            hit.address,
            12);
        out << ',';
        AppendRangeRefsContainingJson(
            out,
            "forum_global_admin",
            forumGlobalAdmin,
            0x10000,
            hit.address,
            12);
        out << "]}";
    }

    out << "]}";
}

uint64_t DecodeWorldBzSingletonList(uint64_t raw)
{
    return OW::ROR64(
        (raw ^ OW::offset::Singleton_K1_xor) - OW::offset::Singleton_K2_sub,
        OW::offset::Singleton_Ror) +
        OW::offset::Singleton_K3_add;
}

uint64_t DecodeHandoffLiveAdminList(uint64_t raw)
{
    return OW::ROR64(
        ((raw + 0x78B568A5D3C8EF76ull) ^ 0x8B846BECDFD77B79ull) +
            0x73978469CB862683ull,
        48);
}

void AppendSingletonProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t base,
    uint64_t adminRva,
    uint64_t inputOffset,
    bool handoffFormula)
{
    uint64_t admin = 0;
    uint64_t raw = 0;
    uint64_t list = 0;
    uint64_t slot6 = 0;
    float sensitivity = 0.0f;
    const bool adminRead = TryReadValue(base + adminRva, admin);
    const bool rawRead =
        OW::IsPlausibleUserPointer(admin) &&
        TryReadValue(admin + inputOffset, raw);
    if (rawRead)
        list = handoffFormula ? DecodeHandoffLiveAdminList(raw)
                              : DecodeWorldBzSingletonList(raw);
    const bool slotRead =
        OW::IsPlausibleUserPointer(list) &&
        TryReadValue(list + 8ull * OW::offset::SensitivitySingletonIndex, slot6);
    const bool sensitivityRead =
        OW::IsPlausibleUserPointer(slot6) &&
        TryReadValue(slot6 + OW::offset::Sensitivity, sensitivity);

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"admin_rva\":";
    AppendHexString(out, adminRva);
    out << ",\"admin_read\":" << (adminRead ? "true" : "false")
        << ",\"admin\":";
    AppendHexOrNull(out, admin);
    out << ",\"admin_plausible\":"
        << (OW::IsPlausibleUserPointer(admin) ? "true" : "false")
        << ",\"input_offset\":";
    AppendHexString(out, inputOffset);
    out << ",\"raw_read\":" << (rawRead ? "true" : "false")
        << ",\"raw\":";
    AppendHexOrNull(out, raw);
    out << ",\"decoded_list\":";
    AppendHexOrNull(out, list);
    out << ",\"decoded_list_plausible\":"
        << (OW::IsPlausibleUserPointer(list) ? "true" : "false")
        << ",\"slot6_read\":" << (slotRead ? "true" : "false")
        << ",\"slot6\":";
    AppendHexOrNull(out, slot6);
    out << ",\"sensitivity_read\":" << (sensitivityRead ? "true" : "false")
        << ",\"sensitivity\":";
    if (sensitivityRead)
        AppendNumberOrNull(out, sensitivity);
    else
        out << "null";
    out << '}';
}

constexpr uint64_t kBzInputOptionsVtableRva = 0x2ED8D60;
constexpr uint64_t kBzModuleProbeSize = 0x5000000;
constexpr float kBzInputOptionsAngleScale = 0.011519159190356731f;

bool ReadFiniteFloat(uint64_t address, float& value)
{
    value = 0.0f;
    return TryReadValue(address, value) && std::isfinite(value);
}

void AppendInputOptionsObjectJson(
    std::ostringstream& out,
    uint64_t moduleBase,
    uint64_t object,
    int slotIndex)
{
    uint64_t vtable = 0;
    uint16_t enabledWord = 0;
    uint8_t enabledByte = 0;
    uint8_t invertY = 0;
    uint64_t qword2230 = 0;
    float inputX = 0.0f;
    float inputY = 0.0f;
    float sensitivity = 0.0f;
    float extra223C = 0.0f;

    const bool objectOk = OW::IsPlausibleUserPointer(object);
    const bool vtableRead = objectOk && TryReadValue(object, vtable);
    const bool vtableInModule =
        vtableRead &&
        vtable >= moduleBase &&
        vtable < moduleBase + kBzModuleProbeSize;
    const bool exactVtable =
        vtableRead && vtable == moduleBase + kBzInputOptionsVtableRva;
    const bool enabledWordRead =
        objectOk && TryReadValue(object + 0x228, enabledWord);
    const bool enabledByteRead =
        objectOk && TryReadValue(object + 0x228, enabledByte);
    const bool invertYRead =
        objectOk && TryReadValue(object + OW::offset::Invert_Y_Flag, invertY);
    const bool inputXRead =
        objectOk && ReadFiniteFloat(object + OW::offset::SensX_Scale, inputX);
    const bool inputYRead =
        objectOk && ReadFiniteFloat(object + OW::offset::SensY_Scale, inputY);
    const bool sensitivityRead =
        objectOk && ReadFiniteFloat(object + OW::offset::Sensitivity, sensitivity);
    const bool extra223CRead =
        objectOk && ReadFiniteFloat(object + OW::offset::Sensitivity + 4, extra223C);
    const bool qword2230Read =
        objectOk && TryReadValue(object + 0x2230, qword2230);
    const bool clampedSensitivity =
        sensitivityRead && sensitivity > 0.0f && sensitivity <= 1.0f;
    const bool finiteInputPair = inputXRead && inputYRead;
    const bool looksLikeInputOptions =
        exactVtable ||
        (vtableInModule && clampedSensitivity && finiteInputPair);

    out << "{\"slot\":" << slotIndex
        << ",\"object\":";
    AppendHexOrNull(out, object);
    out << ",\"object_plausible\":"
        << (objectOk ? "true" : "false")
        << ",\"vtable_read\":"
        << (vtableRead ? "true" : "false")
        << ",\"vtable\":";
    AppendHexOrNull(out, vtable);
    out << ",\"vtable_rva\":";
    if (vtableInModule)
        AppendHexString(out, vtable - moduleBase);
    else
        out << "null";
    out << ",\"matches_static_vtable\":"
        << (exactVtable ? "true" : "false")
        << ",\"looks_like_input_options\":"
        << (looksLikeInputOptions ? "true" : "false")
        << ",\"word_228\":{\"read\":"
        << (enabledWordRead ? "true" : "false")
        << ",\"value\":" << enabledWord
        << "},\"byte_228\":{\"read\":"
        << (enabledByteRead ? "true" : "false")
        << ",\"value\":" << static_cast<unsigned int>(enabledByte)
        << "},\"invert_y_2156\":{\"read\":"
        << (invertYRead ? "true" : "false")
        << ",\"value\":" << static_cast<unsigned int>(invertY)
        << "},\"fields\":[";
    AppendFloatAtJson(out, "input_x_2224", object + OW::offset::SensX_Scale);
    out << ',';
    AppendFloatAtJson(out, "input_y_2228", object + OW::offset::SensY_Scale);
    out << ',';
    AppendFloatAtJson(out, "sensitivity_2238", object + OW::offset::Sensitivity);
    out << ',';
    AppendFloatAtJson(out, "field_223c", object + OW::offset::Sensitivity + 4);
    out << "],\"qword_2230\":{\"read\":"
        << (qword2230Read ? "true" : "false")
        << ",\"value\":";
    AppendHexOrNull(out, qword2230);
    out << "},\"scaled_sensitivity\":";
    if (sensitivityRead)
        AppendNumberOrNull(out, sensitivity * kBzInputOptionsAngleScale);
    else
        out << "null";
    out << ",\"user_sensitivity_from_2238\":";
    if (sensitivityRead && sensitivity >= 0.0001f && sensitivity <= 1.0f)
        AppendNumberOrNull(out, sensitivity * OW::offset::Sensitivity_NormalizedToUserScale);
    else
        out << "null";
    out << '}';
}

bool IsInterestingInputOptionsSlot(uint64_t moduleBase, uint64_t object, int slotIndex)
{
    if (!OW::IsPlausibleUserPointer(object))
        return false;
    if (slotIndex == static_cast<int>(OW::offset::SensitivitySingletonIndex))
        return true;

    uint64_t vtable = 0;
    if (!TryReadValue(object, vtable))
        return false;
    if (vtable == moduleBase + kBzInputOptionsVtableRva)
        return true;

    float sensitivity = 0.0f;
    float inputX = 0.0f;
    float inputY = 0.0f;
    if (vtable >= moduleBase &&
        vtable < moduleBase + kBzModuleProbeSize &&
        ReadFiniteFloat(object + OW::offset::Sensitivity, sensitivity) &&
        sensitivity > 0.0f &&
        sensitivity <= 1.0f &&
        ReadFiniteFloat(object + OW::offset::SensX_Scale, inputX) &&
        ReadFiniteFloat(object + OW::offset::SensY_Scale, inputY)) {
        return true;
    }

    return false;
}

void AppendInputOptionsSlotScanJson(
    std::ostringstream& out,
    const char* name,
    uint64_t moduleBase,
    uint64_t list,
    size_t maxSlots)
{
    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"list\":";
    AppendHexOrNull(out, list);
    out << ",\"list_plausible\":"
        << (OW::IsPlausibleUserPointer(list) ? "true" : "false")
        << ",\"max_slots\":" << maxSlots
        << ",\"slots\":[";

    size_t emitted = 0;
    if (OW::IsPlausibleUserPointer(list)) {
        for (size_t slot = 0; slot < maxSlots; ++slot) {
            uint64_t object = 0;
            if (!TryReadValue(list + 8ull * slot, object) ||
                !IsInterestingInputOptionsSlot(
                    moduleBase,
                    object,
                    static_cast<int>(slot))) {
                continue;
            }

            if (emitted != 0)
                out << ',';
            AppendInputOptionsObjectJson(
                out,
                moduleBase,
                object,
                static_cast<int>(slot));
            ++emitted;
        }
    }

    out << "],\"emitted_slots\":" << emitted << '}';
}

void AppendListDecodeCandidatesJson(
    std::ostringstream& out,
    uint64_t moduleBase,
    uint64_t raw)
{
    out << '[';
    const uint64_t candidates[] = {
        raw,
        DecodeWorldBzSingletonList(raw),
        DecodeHandoffLiveAdminList(raw),
    };
    const char* names[] = {
        "raw",
        "world_decode",
        "live_admin_decode",
    };
    bool emitted = false;
    for (size_t i = 0; i < 3; ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (candidates[j] == candidates[i]) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        if (emitted)
            out << ',';
        emitted = true;
        AppendInputOptionsSlotScanJson(
            out,
            names[i],
            moduleBase,
            candidates[i],
            64);
    }
    out << ']';
}

void AppendAdminInputOptionsProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t moduleBase,
    uint64_t rootRva,
    uint64_t inputOffset)
{
    uint64_t admin = 0;
    uint64_t raw = 0;
    const bool adminRead = TryReadValue(moduleBase + rootRva, admin);
    const bool rawRead =
        OW::IsPlausibleUserPointer(admin) &&
        TryReadValue(admin + inputOffset, raw);

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"root_rva\":";
    AppendHexString(out, rootRva);
    out << ",\"admin_read\":" << (adminRead ? "true" : "false")
        << ",\"admin\":";
    AppendHexOrNull(out, admin);
    out << ",\"input_offset\":";
    AppendHexString(out, inputOffset);
    out << ",\"raw_read\":" << (rawRead ? "true" : "false")
        << ",\"raw\":";
    AppendHexOrNull(out, raw);
    out << ",\"decoded_lists\":";
    if (rawRead)
        AppendListDecodeCandidatesJson(out, moduleBase, raw);
    else
        out << "[]";
    out << '}';
}

void AppendLiveAdminInputOptionsProbeJson(
    std::ostringstream& out,
    uint64_t moduleBase,
    uint64_t liveGameAdmin)
{
    const uint64_t listOffsets[] = {
        OW::offset::Singleton_InputOffset,
        0x168,
        0x2E0,
        0x300,
        0x310,
    };

    out << "{\"live_game_admin\":";
    AppendHexOrNull(out, liveGameAdmin);
    out << ",\"offsets\":[";
    bool emitted = false;
    for (uint64_t offset : listOffsets) {
        uint64_t raw = 0;
        const bool rawRead =
            OW::IsPlausibleUserPointer(liveGameAdmin) &&
            TryReadValue(liveGameAdmin + offset, raw);
        if (emitted)
            out << ',';
        emitted = true;
        out << "{\"offset\":";
        AppendHexString(out, offset);
        out << ",\"raw_read\":" << (rawRead ? "true" : "false")
            << ",\"raw\":";
        AppendHexOrNull(out, raw);
        out << ",\"decoded_lists\":";
        if (rawRead)
            AppendListDecodeCandidatesJson(out, moduleBase, raw);
        else
            out << "[]";
        out << '}';
    }
    out << "]}";
}

void AppendBzInputOptionsStaticProbeJson(
    std::ostringstream& out,
    uint64_t moduleBase,
    uint64_t liveGameAdmin)
{
    out << "{\"static_vtable_rva\":";
    AppendHexString(out, kBzInputOptionsVtableRva);
    out << ",\"reader_scale\":";
    AppendNumberOrNull(out, kBzInputOptionsAngleScale);
    out << ",\"admin_roots\":[";
    AppendAdminInputOptionsProbeJson(
        out,
        "ida_global_admin_3a71930",
        moduleBase,
        0x3A71930,
        OW::offset::Singleton_InputOffset);
    out << ',';
    AppendAdminInputOptionsProbeJson(
        out,
        "forum_global_admin_3a65930",
        moduleBase,
        0x3A65930,
        OW::offset::Singleton_InputOffset);
    out << ',';
    AppendAdminInputOptionsProbeJson(
        out,
        "client_game_3a5fbc8_live_admin",
        moduleBase,
        0x3A5FBC8,
        OW::offset::LiveGameAdmin_InputOffset);
    out << "],\"live_admin_lists\":";
    AppendLiveAdminInputOptionsProbeJson(out, moduleBase, liveGameAdmin);
    out << '}';
}

void AppendViewMatrixChainProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t decoded,
    uint64_t parentOffset)
{
    uint64_t p1 = 0;
    uint64_t p2 = 0;
    uint64_t p3 = 0;
    uint64_t p4 = 0;
    const bool p1Read =
        OW::IsPlausibleUserPointer(decoded) &&
        TryReadValue(decoded + OW::offset::VM_P1, p1);
    const bool p2Read =
        OW::IsPlausibleUserPointer(p1) &&
        TryReadValue(p1 + OW::offset::VM_P2, p2);
    const bool p3Read =
        OW::IsPlausibleUserPointer(p2) &&
        TryReadValue(p2 + parentOffset, p3);
    const bool p4Read =
        OW::IsPlausibleUserPointer(p3) &&
        TryReadValue(p3 + OW::offset::VM_ViewProjectionPtr, p4);

    OW::Matrix cameraView{};
    OW::Matrix projection{};
    const bool cameraRead =
        OW::IsPlausibleUserPointer(p2) &&
        TryReadValue(p2 + OW::offset::VM_ViewMatrix, cameraView);
    const bool projectionRead =
        OW::IsPlausibleUserPointer(p2) &&
        TryReadValue(p2 + OW::offset::VM_ProjMatrix, projection);
    const OW::Matrix combined = OW::ComposeCameraProjection(cameraView, projection);
    const bool combinedValid =
        cameraRead && projectionRead && ProbeMatrixNonIdentity(combined);

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"parent_offset\":";
    AppendHexString(out, parentOffset);
    out << ",\"p1_read\":" << (p1Read ? "true" : "false")
        << ",\"p1\":";
    AppendHexOrNull(out, p1);
    out << ",\"p2_read\":" << (p2Read ? "true" : "false")
        << ",\"p2\":";
    AppendHexOrNull(out, p2);
    out << ",\"p3_read\":" << (p3Read ? "true" : "false")
        << ",\"p3\":";
    AppendHexOrNull(out, p3);
    out << ",\"p4_read\":" << (p4Read ? "true" : "false")
        << ",\"p4\":";
    AppendHexOrNull(out, p4);
    out << ",\"render_view_projection\":";
    AppendMatrixProbeJson(out, p4 ? p4 + OW::offset::VM_ViewProjectionMatrix : 0);
    out << ",\"camera_view\":{\"address\":";
    AppendHexOrNull(out, p2 ? p2 + OW::offset::VM_ViewMatrix : 0);
    out << ",\"read\":" << (cameraRead ? "true" : "false")
        << ",\"non_identity\":"
        << (cameraRead && ProbeMatrixNonIdentity(cameraView) ? "true" : "false")
        << ",\"camera_view_plausible\":"
        << (cameraRead && ProbeCameraViewMatrixPlausible(cameraView) ? "true" : "false")
        << "},\"projection\":{\"address\":";
    AppendHexOrNull(out, p2 ? p2 + OW::offset::VM_ProjMatrix : 0);
    out << ",\"read\":" << (projectionRead ? "true" : "false")
        << ",\"non_identity\":"
        << (projectionRead && ProbeMatrixNonIdentity(projection) ? "true" : "false")
        << "},\"camera_projection_combined_non_identity\":"
        << (combinedValid ? "true" : "false") << '}';
}

void AppendViewMatrixRootProbeJson(
    std::ostringstream& out,
    const char* name,
    uint64_t base,
    uint64_t rootRva,
    uint64_t key1,
    uint64_t key2,
    uint64_t key3,
    const char* formula)
{
    uint64_t root = 0;
    const bool rootRead = TryReadValue(base + rootRva, root);
    uint64_t decoded = 0;
    if (rootRead) {
        if (std::string(formula) == "sub_xor_sub")
            decoded = ((root - key1) ^ key2) - key3;
        else
            decoded = (root + key1) ^ key2;
    }

    out << "{\"name\":";
    AppendJsonString(out, name);
    out << ",\"root_rva\":";
    AppendHexString(out, rootRva);
    out << ",\"root_read\":" << (rootRead ? "true" : "false")
        << ",\"root\":";
    AppendHexOrNull(out, root);
    out << ",\"formula\":";
    AppendJsonString(out, formula);
    out << ",\"decoded\":";
    AppendHexOrNull(out, decoded);
    out << ",\"decoded_plausible\":"
        << (OW::IsPlausibleUserPointer(decoded) ? "true" : "false")
        << ",\"direct_1c0\":";
    AppendMatrixProbeJson(out, decoded + OW::offset::Bz150818_DirectViewProjectionMatrix);
    out << ",\"chain_6c8\":";
    AppendViewMatrixChainProbeJson(out, "chain_6c8", decoded, 0x6C8);
    out << ",\"chain_6d8\":";
    AppendViewMatrixChainProbeJson(out, "chain_6d8", decoded, 0x6D8);
    out << '}';
}

void AppendCandidateSlotSelectedBooleanJson(
    std::ostringstream& out,
    const OW::HeroPerks::CandidateSlotSelectedBoolean& selected)
{
    out << "{\"evidence\":\"20260615_candidate_slot_family_fail_closed\","
        << "\"supported_hero\":" << (selected.supportedHero ? "true" : "false")
        << ",\"available\":" << (selected.available ? "true" : "false")
        << ",\"selected\":";
    if (selected.available)
        out << (selected.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"result\":";
    AppendJsonString(out, selected.result ? selected.result : "");
    out << ",\"family\":";
    if (selected.family && selected.family[0] != '\0')
        AppendJsonString(out, selected.family);
    else
        out << "null";
    out << ",\"confidence\":";
    AppendJsonString(out, selected.confidence ? selected.confidence : "none");
    out << ",\"side\":";
    if (selected.side && selected.side[0] != '\0')
        AppendJsonString(out, selected.side);
    else
        out << "null";
    out << ",\"matched_offsets\":[";
    for (size_t index = 0; index < selected.matchedOffsetCount; ++index) {
        if (index != 0)
            out << ',';
        AppendHexString(out, selected.matchedOffsets[index]);
    }
    out << "],\"used_signals\":{"
        << "\"candidate_slots\":true,"
        << "\"e78\":false,"
        << "\"ultimate_charge\":false,"
        << "\"legacy_keys\":false"
        << "}}";
}

void AppendRawSelectedBooleanJson(
    std::ostringstream& out,
    const OW::HeroPerks::RawSelectedBoolean& selected)
{
    out << "{\"evidence\":\"20260615_raw_per_hero_fail_closed\","
        << "\"supported_hero\":" << (selected.supportedHero ? "true" : "false")
        << ",\"available\":" << (selected.available ? "true" : "false")
        << ",\"selected\":";
    if (selected.available)
        out << (selected.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"rule\":";
    if (selected.rule && selected.rule[0] != '\0')
        AppendJsonString(out, selected.rule);
    else
        out << "null";
    out << ",\"used_signals\":{"
        << "\"skill_16c2\":true,"
        << "\"symmetra_02e8\":true,"
        << "\"zarya_component55_270\":true,"
        << "\"e78\":false,"
        << "\"ultimate_charge\":false,"
        << "\"legacy_keys\":false"
        << "}}";
}

void AppendAnaHeadshotSelectedBooleanJson(
    std::ostringstream& out,
    const OW::HeroPerks::AnaHeadshotSelectedBoolean& selected)
{
    out << "{\"evidence\":\"20260616_ana_e44_not_no_pri_component21_02e8_headshot_major_right\","
        << "\"supported_hero\":" << (selected.supportedHero ? "true" : "false")
        << ",\"available\":" << (selected.available ? "true" : "false")
        << ",\"selected\":";
    if (selected.available)
        out << (selected.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"result\":";
    AppendJsonString(out, selected.result ? selected.result : "");
    out << ",\"primary_gate_e44_read\":" << (selected.primaryGateRead ? "true" : "false")
        << ",\"primary_gate_e44_u32\":";
    AppendHexString(out, selected.primaryGateE44);
    out << ",\"primary_gate_active\":" << (selected.primaryGateActive ? "true" : "false")
        << ",\"major_selected_known\":" << (selected.majorSelectedKnown ? "true" : "false")
        << ",\"major_selected\":" << (selected.majorSelected ? "true" : "false")
        << ",\"skill_02e8_read\":" << (selected.skill02E8Read ? "true" : "false")
        << ",\"skill_02e8_u64\":";
    AppendHexString(out, selected.skill02E8);
    out << ",\"skill_02e8_high32\":";
    AppendHexString(out, selected.skill02E8High32);
    out << ",\"skill_02e8_low32\":";
    AppendHexString(out, selected.skill02E8Low32);
    out << ",\"skill_02e8_gate_known\":"
        << (selected.skill02E8GateKnown ? "true" : "false")
        << ",\"skill_02e8_gate_active\":"
        << (selected.skill02E8GateActive ? "true" : "false")
        << ",\"skill_0348_target_read\":"
        << (selected.skill0348TargetRead ? "true" : "false")
        << ",\"skill_0348_target\":";
    AppendHexOrNull(out, selected.skill0348Target);
    out << ",\"skill_0348_target_plausible\":"
        << (selected.skill0348TargetPlausible ? "true" : "false")
        << ",\"skill_0348_target_01d4_read\":"
        << (selected.skill0348Target1D4Read ? "true" : "false")
        << ",\"skill_0348_target_01d4_u32\":";
    AppendHexString(out, selected.skill0348Target1D4);
    out << ",\"skill_0348_target_01d4_qword_read\":"
        << (selected.skill0348Target1D4QwordRead ? "true" : "false")
        << ",\"skill_0348_target_01d4_u64\":";
    AppendHexString(out, selected.skill0348Target1D4Qword);
    out << ",\"component21_base\":";
    AppendHexOrNull(out, selected.component21Base);
    out << ",\"component21_021c_read\":"
        << (selected.component21_021CRead ? "true" : "false")
        << ",\"component21_021c_u32\":";
    AppendHexString(out, selected.component21_021C);
    out << ",\"component21_0228_read\":"
        << (selected.component21_0228Read ? "true" : "false")
        << ",\"component21_0228_u32\":";
    AppendHexString(out, selected.component21_0228);
    out << ",\"component21_0228_qword_read\":"
        << (selected.component21_0228QwordRead ? "true" : "false")
        << ",\"component21_0228_u64\":";
    AppendHexString(out, selected.component21_0228Qword);
    out << ",\"component21_02e0_read\":"
        << (selected.component21_02E0Read ? "true" : "false")
        << ",\"component21_02e0_u32\":";
    AppendHexString(out, selected.component21_02E0);
    out << ",\"component21_02e8_read\":"
        << (selected.component21_02E8Read ? "true" : "false")
        << ",\"component21_02e8_u32\":";
    AppendHexString(out, selected.component21_02E8);
    out << ",\"component21_02e8_qword_read\":"
        << (selected.component21_02E8QwordRead ? "true" : "false")
        << ",\"component21_02e8_u64\":";
    AppendHexString(out, selected.component21_02E8Qword);
    out << ",\"component7f_base\":";
    AppendHexOrNull(out, selected.component7fBase);
    out << ",\"component7f_d4_read\":" << (selected.component7fD4Read ? "true" : "false")
        << ",\"component7f_d4_u64\":";
    AppendHexString(out, selected.component7fD4);
    out << ",\"component7f_d8_read\":" << (selected.component7fD8Read ? "true" : "false")
        << ",\"component7f_d8_u32\":";
    AppendHexString(out, selected.component7fD8);
    out << ",\"component7f_d8_qword_read\":"
        << (selected.component7fD8QwordRead ? "true" : "false")
        << ",\"component7f_d8_u64\":";
    AppendHexString(out, selected.component7fD8Qword);
    out << ",\"component7f_scan\":{\"range_begin\":";
    AppendHexString(out, OW::HeroPerks::kAnaComponent7fScanBegin);
    out << ",\"range_end_exclusive\":";
    AppendHexString(out, OW::HeroPerks::kAnaComponent7fScanEnd);
    out << ",\"stride\":";
    AppendHexString(out, OW::HeroPerks::kAnaComponent7fScanStride);
    out << ",\"hash\":";
    AppendHexString(out, selected.component7fScanHash);
    out << ",\"read_count\":" << selected.component7fScanReadCount
        << ",\"nonzero_count\":" << selected.component7fScanNonzeroCount
        << ",\"nonzero_qwords\":[";
    bool firstScanValue = true;
    for (size_t index = 0; index < OW::HeroPerks::kAnaComponent7fScanQwordCount; ++index) {
        if (!selected.component7fScanRead[index] || selected.component7fScanQword[index] == 0)
            continue;
        if (!firstScanValue)
            out << ',';
        firstScanValue = false;
        out << "{\"offset\":";
        AppendHexString(
            out,
            OW::HeroPerks::kAnaComponent7fScanBegin +
                static_cast<uint64_t>(index) * OW::HeroPerks::kAnaComponent7fScanStride);
        out << ",\"u64\":";
        AppendHexString(out, selected.component7fScanQword[index]);
        out << '}';
    }
    out << "]}";
    out << ",\"component7f_raw_slot\":{";
    out << "\"bitset_read\":" << (selected.component7fRawSlotBitsetRead ? "true" : "false")
        << ",\"present\":" << (selected.component7fRawSlotPresent ? "true" : "false")
        << ",\"index_read\":" << (selected.component7fRawSlotIndexRead ? "true" : "false")
        << ",\"table_read\":" << (selected.component7fRawSlotTableRead ? "true" : "false")
        << ",\"table_plausible\":" << (selected.component7fRawSlotTablePlausible ? "true" : "false")
        << ",\"slot_read\":" << (selected.component7fRawSlotRead ? "true" : "false")
        << ",\"slot_plausible\":" << (selected.component7fRawSlotPlausible ? "true" : "false")
        << ",\"bitset\":";
    AppendHexString(out, selected.component7fRawSlotBitset);
    out << ",\"index_base\":";
    AppendHexString(out, selected.component7fRawSlotIndexBase);
    out << ",\"component_index\":";
    AppendHexString(out, selected.component7fRawSlotComponentIndex);
    out << ",\"table\":";
    AppendHexOrNull(out, selected.component7fRawSlotTable);
    out << ",\"base\":";
    AppendHexOrNull(out, selected.component7fRawSlotBase);
    out << ",\"d8_read\":" << (selected.component7fRawSlotD8Read ? "true" : "false")
        << ",\"d8_u32\":";
    AppendHexString(out, selected.component7fRawSlotD8);
    out << ",\"d8_qword_read\":"
        << (selected.component7fRawSlotD8QwordRead ? "true" : "false")
        << ",\"d8_u64\":";
    AppendHexString(out, selected.component7fRawSlotD8Qword);
    out << ",\"scan\":{\"range_begin\":";
    AppendHexString(out, OW::HeroPerks::kAnaComponent7fRawSlotScanBegin);
    out << ",\"range_end_exclusive\":";
    AppendHexString(out, OW::HeroPerks::kAnaComponent7fRawSlotScanEnd);
    out << ",\"stride\":";
    AppendHexString(out, OW::HeroPerks::kAnaComponent7fRawSlotScanStride);
    out << ",\"hash\":";
    AppendHexString(out, selected.component7fRawSlotScanHash);
    out << ",\"read_count\":" << selected.component7fRawSlotScanReadCount
        << ",\"nonzero_count\":" << selected.component7fRawSlotScanNonzeroCount
        << ",\"nonzero_qwords\":[";
    bool firstRawSlotScanValue = true;
    for (size_t index = 0; index < OW::HeroPerks::kAnaComponent7fRawSlotScanQwordCount; ++index) {
        if (!selected.component7fRawSlotScanRead[index] ||
            selected.component7fRawSlotScanQword[index] == 0)
            continue;
        if (!firstRawSlotScanValue)
            out << ',';
        firstRawSlotScanValue = false;
        out << "{\"offset\":";
        AppendHexString(
            out,
            OW::HeroPerks::kAnaComponent7fRawSlotScanBegin +
                static_cast<uint64_t>(index) * OW::HeroPerks::kAnaComponent7fRawSlotScanStride);
        out << ",\"u64\":";
        AppendHexString(out, selected.component7fRawSlotScanQword[index]);
        out << '}';
    }
    out << "]}}";
    out << ",\"candidate_diagnostics\":{";
    out << "\"skill_09a_read\":" << (selected.skill09ARead ? "true" : "false")
        << ",\"skill_09a_u16\":";
    AppendHexString(out, selected.skill09A);
    out << ",\"skill_592_read\":" << (selected.skill592Read ? "true" : "false")
        << ",\"skill_592_u16\":";
    AppendHexString(out, selected.skill592);
    out << ",\"skill_0bd0_read\":" << (selected.skill0BD0Read ? "true" : "false")
        << ",\"skill_0bd0_u64\":";
    AppendHexString(out, selected.skill0BD0);
    out << ",\"component22_base\":";
    AppendHexOrNull(out, selected.component22Base);
    out << ",\"component22_0140_read\":" << (selected.component22_0140Read ? "true" : "false")
        << ",\"component22_0140_u16\":";
    AppendHexString(out, selected.component22_0140);
    out << ",\"component22_01f2_read\":" << (selected.component22_01F2Read ? "true" : "false")
        << ",\"component22_01f2_u16\":";
    AppendHexString(out, selected.component22_01F2);
    out << ",\"component22_0200_read\":" << (selected.component22_0200Read ? "true" : "false")
        << ",\"component22_0200_u32\":";
    AppendHexString(out, selected.component22_0200);
    out << ",\"component22_0202_read\":" << (selected.component22_0202Read ? "true" : "false")
        << ",\"component22_0202_u16\":";
    AppendHexString(out, selected.component22_0202);
    out << ",\"component22_0203_read\":" << (selected.component22_0203Read ? "true" : "false")
        << ",\"component22_0203_u8\":";
    AppendHexString(out, selected.component22_0203);
    out << ",\"component22_0350_read\":" << (selected.component22_0350Read ? "true" : "false")
        << ",\"component22_0350_u16\":";
    AppendHexString(out, selected.component22_0350);
    out << '}';
    out << ",\"used_signals\":{"
        << "\"statescript_major_selected\":false,"
        << "\"component21_02e8\":true,"
        << "\"component21_0228\":false,"
        << "\"e44_primary_gate\":true,"
        << "\"skill_0348_target_01d4\":false,"
        << "\"skill_02e8_gate\":false,"
        << "\"component7f_d4_d8\":false,"
        << "\"component7f_scan\":false,"
        << "\"component7f_raw_slot\":false,"
        << "\"ana_candidate_diagnostics\":false,"
        << "\"e78\":false,"
        << "\"ultimate_charge\":false,"
        << "\"legacy_keys\":false"
        << "}}";
}

void AppendMergedSelectedBooleanJson(
    std::ostringstream& out,
    const OW::HeroPerks::MergedSelectedBoolean& selected)
{
    out << "{\"evidence\":\"20260615_merged_fail_closed\","
        << "\"supported_hero\":" << (selected.supportedHero ? "true" : "false")
        << ",\"available\":" << (selected.available ? "true" : "false")
        << ",\"selected\":";
    if (selected.available)
        out << (selected.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"result\":";
    AppendJsonString(out, selected.result ? selected.result : "");
    out << ",\"source\":";
    if (selected.source && selected.source[0] != '\0')
        AppendJsonString(out, selected.source);
    else
        out << "null";
    out << ",\"rule\":";
    if (selected.rule && selected.rule[0] != '\0')
        AppendJsonString(out, selected.rule);
    else
        out << "null";
    out << ",\"used_signals\":{"
        << "\"raw\":true,"
        << "\"candidate_slots\":true,"
        << "\"e78\":false,"
        << "\"ultimate_charge\":false,"
        << "\"legacy_keys\":false"
        << "}}";
}

void AppendStateScriptEa0cRecordSignalJson(
    std::ostringstream& out,
    const OW::HeroPerks::StateScriptEa0cRecordSignal& signal)
{
    out << "{\"found\":" << (signal.found ? "true" : "false")
        << ",\"qword_0050_read\":" << (signal.qword50Read ? "true" : "false")
        << ",\"qword_0050\":";
    AppendHexString(out, signal.qword50);
    out << ",\"qword_0058_read\":" << (signal.qword58Read ? "true" : "false")
        << ",\"qword_0058\":";
    AppendHexString(out, signal.qword58);
    out << '}';
}

void AppendStateScriptEa0cSelectedBooleanJson(
    std::ostringstream& out,
    const OW::HeroPerks::StateScriptEa0cSelectedBoolean& selected)
{
    out << "{\"evidence\":\"20260615_ea0c_10127_unique_candidate_fail_closed\","
        << "\"supported_hero\":" << (selected.supportedHero ? "true" : "false")
        << ",\"available\":" << (selected.available ? "true" : "false")
        << ",\"selected\":";
    if (selected.selectedKnown)
        out << (selected.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"result\":";
    AppendJsonString(out, selected.result ? selected.result : "");
    out << ",\"resolution\":";
    AppendJsonString(out, selected.resolution ? selected.resolution : "");
    out << ",\"source_name\":\"skill_qword_c0_storage\""
        << ",\"hero_map_known\":" << (selected.heroMapKnown ? "true" : "false")
        << ",\"mapped_source_offset\":";
    if (selected.heroMapKnown)
        AppendHexString(out, selected.mappedSourceOffset);
    else
        out << "null";
    out << ",\"map_consistent\":" << (selected.mapConsistent ? "true" : "false")
        << ",\"known_candidate_count\":" << selected.knownCandidateCount
        << ",\"source_found\":" << (selected.sourceFound ? "true" : "false")
        << ",\"matched_source_offset\":";
    if (selected.sourceFound)
        AppendHexString(out, selected.matchedSourceOffset);
    else
        out << "null";
    out << ",\"used_signals\":{"
        << "\"statescript_record_qwords\":true,"
        << "\"e78\":false,"
        << "\"ultimate_charge\":false,"
        << "\"legacy_keys\":false"
        << "},\"records\":{\"0x0000EA0C\":";
    AppendStateScriptEa0cRecordSignalJson(out, selected.recordEa0c);
    out << ",\"0x00010127\":";
    AppendStateScriptEa0cRecordSignalJson(out, selected.record10127);
    out << "}}";
}

void AppendCandidateSlotRawJson(
    std::ostringstream& out,
    const OW::HeroPerks::CandidateSlot& slot)
{
    const OW::HeroPerks::CompletionArraySignature& completion = slot.targetCompletion;
    out << "{\"offset\":";
    AppendHexString(out, slot.offset);
    out << ",\"qword_read\":" << (slot.qwordRead ? "true" : "false")
        << ",\"qword\":";
    AppendHexString(out, slot.qword);
    out << ",\"pointer_plausible\":" << (slot.pointerPlausible ? "true" : "false")
        << ",\"target_semantic_signature\":";
    AppendHexString(out, slot.targetSemanticSignature);
    out << ",\"target_completion\":{"
        << "\"count_u32_read\":" << (completion.countU32Read ? "true" : "false")
        << ",\"count_u32\":";
    AppendHexString(out, completion.countU32);
    out << ",\"count_plausible\":" << (completion.countPlausible ? "true" : "false")
        << ",\"values_read\":" << completion.valuesRead
        << ",\"values_u32_hex\":[";
    for (size_t index = 0; index < completion.valuesRead; ++index) {
        if (index != 0)
            out << ',';
        AppendHexString(out, completion.values[index]);
    }
    out << "]}}";
}

void AppendCandidateSlotsJson(std::ostringstream& out, const OW::HeroPerks::State& perk)
{
    out << '[';
    for (size_t index = 0; index < perk.candidateSlots.size(); ++index) {
        if (index != 0)
            out << ',';
        AppendCandidateSlotRawJson(out, perk.candidateSlots[index]);
    }
    out << ']';
}

std::string RosterStateName(OW::EntityRosterState state)
{
    switch (state) {
    case OW::EntityRosterState::Fresh: return "fresh";
    case OW::EntityRosterState::Missing: return "missing";
    case OW::EntityRosterState::Dead: return "dead";
    default: return "unknown";
    }
}

uint64_t EntityKey(const OW::c_entity& entity)
{
    if (entity.address != 0)
        return entity.address;
    if (entity.LinkBase != 0)
        return entity.LinkBase;
    return entity.roster_key;
}

bool EntityFreshAndAlive(const OW::c_entity& entity)
{
    return entity.roster_state == OW::EntityRosterState::Fresh && entity.Alive;
}

const char* MotionKindName(OW::EntityMotionState::Kind kind)
{
    switch (kind) {
    case OW::EntityMotionState::Kind::Grounded: return "grounded";
    case OW::EntityMotionState::Kind::AirborneRising: return "airborne_rising";
    case OW::EntityMotionState::Kind::AirborneApex: return "airborne_apex";
    case OW::EntityMotionState::Kind::AirborneFalling: return "airborne_falling";
    case OW::EntityMotionState::Kind::Strafing: return "strafing";
    case OW::EntityMotionState::Kind::SuddenStop: return "sudden_stop";
    case OW::EntityMotionState::Kind::TeleportOrInvalid: return "teleport_or_invalid";
    case OW::EntityMotionState::Kind::Unknown:
    default: return "unknown";
    }
}

float DiagnosticGroundedBaselineY(uint64_t key, const OW::c_entity& entity, const OW::EntityMotionState& motion)
{
    static std::mutex baselineMutex;
    static std::unordered_map<uint64_t, float> baselines;

    if (key == 0 || !IsFiniteVector(entity.pos))
        return (std::numeric_limits<float>::quiet_NaN)();

    std::lock_guard<std::mutex> lock(baselineMutex);
    float& baseline = baselines[key];
    if (baseline == 0.0f ||
        (motion.kind == OW::EntityMotionState::Kind::Grounded &&
         std::fabs(motion.verticalVelocity) < 0.35f)) {
        baseline = entity.pos.Y;
    }
    return baseline;
}

std::string HeroName(uint64_t heroId, uint64_t linkBase = 0)
{
    if (heroId == 0)
        return {};

    std::string name = OW::GetHeroEngNames(heroId, linkBase);
    if (!name.empty() && name != "Unknown")
        return name;

    return "Unknown";
}

void AppendCommonStart(std::ostringstream& out, const std::vector<std::string>& warnings = {})
{
    out << "{\"ok\":true,\"schema_version\":" << kSchemaVersion
        << ",\"timestamp_ms\":" << TimestampMs();
    if (!warnings.empty()) {
        out << ",\"warnings\":";
        AppendStringArray(out, warnings);
    }
}

void AppendErrorBody(std::ostringstream& out, int statusCode, const std::string& message)
{
    out << "{\"ok\":false,\"schema_version\":" << kSchemaVersion
        << ",\"timestamp_ms\":" << TimestampMs()
        << ",\"error\":{\"status\":" << statusCode << ",\"message\":";
    AppendJsonString(out, message);
    out << "}}";
}

std::string UrlDecode(const std::string& value)
{
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch == '+') {
            decoded.push_back(' ');
        } else if (ch == '%' && index + 2 < value.size() &&
                   std::isxdigit(static_cast<unsigned char>(value[index + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(value[index + 2]))) {
            const char hex[] = { value[index + 1], value[index + 2], '\0' };
            decoded.push_back(static_cast<char>(std::strtoul(hex, nullptr, 16)));
            index += 2;
        } else {
            decoded.push_back(ch);
        }
    }
    return decoded;
}

std::unordered_map<std::string, std::string> ParseQuery(std::string query)
{
    std::unordered_map<std::string, std::string> parsed;
    while (!query.empty()) {
        const size_t amp = query.find('&');
        const std::string part = query.substr(0, amp);
        query = amp == std::string::npos ? std::string{} : query.substr(amp + 1);

        if (part.empty())
            continue;

        const size_t eq = part.find('=');
        const std::string key = UrlDecode(part.substr(0, eq));
        const std::string value = eq == std::string::npos ? std::string{} : UrlDecode(part.substr(eq + 1));
        if (!key.empty())
            parsed[key] = value;
    }
    return parsed;
}

std::optional<RequestTarget> ParseRequestTarget(const std::string& request)
{
    const size_t lineEnd = request.find("\r\n");
    const std::string requestLine = request.substr(0, lineEnd);
    const size_t methodEnd = requestLine.find(' ');
    if (methodEnd == std::string::npos)
        return std::nullopt;

    const std::string method = requestLine.substr(0, methodEnd);
    if (method != "GET")
        return RequestTarget{ "__method_not_allowed__", {} };

    const size_t targetStart = methodEnd + 1;
    const size_t targetEnd = requestLine.find(' ', targetStart);
    if (targetEnd == std::string::npos || targetEnd <= targetStart)
        return std::nullopt;

    std::string target = requestLine.substr(targetStart, targetEnd - targetStart);
    const size_t queryStart = target.find('?');
    RequestTarget parsed{};
    parsed.path = queryStart == std::string::npos ? target : target.substr(0, queryStart);
    if (queryStart != std::string::npos)
        parsed.query = ParseQuery(target.substr(queryStart + 1));
    return parsed;
}

bool ParseBool(const std::string& value, bool fallback)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char ch : value)
        normalized.push_back(static_cast<char>(std::tolower(ch)));

    if (normalized == "1" || normalized == "true" || normalized == "yes")
        return true;
    if (normalized == "0" || normalized == "false" || normalized == "no")
        return false;
    return fallback;
}

float ParseFloatQuery(
    const std::unordered_map<std::string, std::string>& query,
    const char* key,
    float fallback,
    std::vector<std::string>& warnings)
{
    const auto it = query.find(key);
    if (it == query.end())
        return fallback;

    char* end = nullptr;
    const float value = std::strtof(it->second.c_str(), &end);
    if (end == it->second.c_str() || *end != '\0' || !std::isfinite(value)) {
        warnings.emplace_back(std::string("invalid query parameter: ") + key);
        return fallback;
    }
    return value;
}

int ParseIntQuery(
    const std::unordered_map<std::string, std::string>& query,
    const char* key,
    int fallback,
    std::vector<std::string>& warnings)
{
    const auto it = query.find(key);
    if (it == query.end())
        return fallback;

    char* end = nullptr;
    const long value = std::strtol(it->second.c_str(), &end, 10);
    if (end == it->second.c_str() || *end != '\0') {
        warnings.emplace_back(std::string("invalid query parameter: ") + key);
        return fallback;
    }
    return static_cast<int>(value);
}

EntityQuery ParseEntityQuery(const std::unordered_map<std::string, std::string>& query)
{
    EntityQuery parsed{};
    parsed.maxDistanceM = ParseFloatQuery(query, "max_distance_m", parsed.maxDistanceM, parsed.warnings);
    parsed.maxDistanceM = std::clamp(parsed.maxDistanceM, 0.0f, 10000.0f);
    parsed.limit = std::clamp(ParseIntQuery(query, "limit", parsed.limit, parsed.warnings), 0, 256);

    if (const auto it = query.find("team"); it != query.end()) {
        parsed.team = it->second;
        std::string normalized;
        normalized.reserve(parsed.team.size());
        for (const unsigned char ch : parsed.team)
            normalized.push_back(static_cast<char>(std::tolower(ch)));
        if (normalized != "enemy" && normalized != "ally" && normalized != "all") {
            parsed.warnings.emplace_back("invalid team query parameter; using enemy");
            normalized = "enemy";
        }
        parsed.team = normalized;
    }

    if (const auto it = query.find("include_dead"); it != query.end())
        parsed.includeDead = ParseBool(it->second, parsed.includeDead);

    if (const auto it = query.find("team_debug"); it != query.end())
        parsed.teamDebug = ParseBool(it->second, parsed.teamDebug);

    return parsed;
}

bool TeamMatches(const OW::c_entity& entity, const std::string& requestedTeam)
{
    if (requestedTeam == "all")
        return true;
    if (requestedTeam == "ally")
        return !entity.Team;
    return entity.Team;
}

std::string BuildHealthJson()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out);
    out << ",\"process\":{\"connected\":" << (OW::ProcessConnection::IsConnected() ? "true" : "false")
        << ",\"connecting\":" << (OW::ProcessConnection::IsConnecting() ? "true" : "false")
        << ",\"pid\":" << OW::ProcessConnection::ConnectedPid()
        << ",\"base_address\":";
    AppendHexOrNull(out, OW::ProcessConnection::ConnectedBaseAddress());
    out << ",\"status_text\":";
    AppendJsonString(out, OW::ProcessConnection::StatusText());
    out << "},\"test_server\":{\"read_only\":true,\"write_armed\":false,\"bind\":";
    AppendJsonString(out, kBindAddress);
    out << ",\"port\":" << g_port.load(std::memory_order_acquire) << ",\"cors_mode\":";
    AppendJsonString(out, g_allowWildcardCors.load(std::memory_order_acquire) ? "wildcard" : "disabled");
    out << "}}";
    return out.str();
}

std::string BuildDiagnosticsJson()
{
    const Diagnostics::StatusSnapshot snapshot = Diagnostics::Snapshot();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out);
    out << ",\"diagnostics\":{"
        << "\"dma_ready\":" << (snapshot.dmaReady ? "true" : "false")
        << ",\"process_attached\":" << (snapshot.processAttached ? "true" : "false")
        << ",\"entity_scan_hz\":";
    AppendNumberOrNull(out, static_cast<float>(snapshot.entityScanHz));
    out << ",\"entity_process_hz\":";
    AppendNumberOrNull(out, static_cast<float>(snapshot.entityProcessHz));
    out << ",\"entity_count\":" << snapshot.entityCount
        << ",\"last_scan_entity_count\":" << snapshot.lastScanEntityCount
        << ",\"entity_scan_cycles\":" << snapshot.entityScanCycles
        << ",\"entity_process_cycles\":" << snapshot.entityProcessCycles
        << ",\"view_matrix_ok\":" << (snapshot.viewMatrixResolved && snapshot.viewMatrixValid ? "true" : "false")
        << ",\"view_matrix_resolved\":" << (snapshot.viewMatrixResolved ? "true" : "false")
        << ",\"view_matrix_valid\":" << (snapshot.viewMatrixValid ? "true" : "false")
        << ",\"key_status\":";
    AppendJsonString(out, Diagnostics::ToString(snapshot.keyStatus));
    out << ",\"fps\":";
    AppendNumberOrNull(out, static_cast<float>(snapshot.fps));
    out << ",\"dma_reads\":{\"total\":" << snapshot.dmaReads.total
        << ",\"succeeded\":" << snapshot.dmaReads.succeeded
        << ",\"failed\":" << snapshot.dmaReads.failed
        << ",\"avg_latency_us\":" << snapshot.dmaReads.avgLatencyUs
        << ",\"max_latency_us\":" << snapshot.dmaReads.maxLatencyUs
        << "},\"roster\":{\"fresh\":" << snapshot.roster.fresh
        << ",\"dead\":" << snapshot.roster.dead
        << ",\"missing\":" << snapshot.roster.missing
        << ",\"expired\":" << snapshot.roster.expired
        << ",\"hero_changed\":" << snapshot.roster.heroChanged
        << "},\"entity_scan_detail\":{\"entity_list\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.entityList);
    out << ",\"readable_bytes\":" << snapshot.entityScanDetail.readableBytes
        << ",\"readable_chunks\":" << snapshot.entityScanDetail.readableChunks
        << ",\"slots_scanned\":" << snapshot.entityScanDetail.slotsScanned
        << ",\"nonzero_slots\":" << snapshot.entityScanDetail.nonZeroSlots
        << ",\"plausible_slots\":" << snapshot.entityScanDetail.plausibleSlots
        << ",\"records\":" << snapshot.entityScanDetail.records
        << ",\"match_ids\":" << snapshot.entityScanDetail.matchIds
        << ",\"link_present\":" << snapshot.entityScanDetail.linkPresent
        << ",\"link_uid_nonzero\":" << snapshot.entityScanDetail.linkUidNonZero
        << ",\"link_matched\":" << snapshot.entityScanDetail.linkMatched
        << ",\"link_pairs\":" << snapshot.entityScanDetail.linkPairs
        << ",\"self_health_base\":" << snapshot.entityScanDetail.selfHealthBase
        << ",\"self_health_read\":" << snapshot.entityScanDetail.selfHealthRead
        << ",\"self_health_plausible\":" << snapshot.entityScanDetail.selfHealthPlausible
        << ",\"self_hero_base\":" << snapshot.entityScanDetail.selfHeroBase
        << ",\"self_hero_read\":" << snapshot.entityScanDetail.selfHeroRead
        << ",\"self_hero_known\":" << snapshot.entityScanDetail.selfHeroKnown
        << ",\"self_velocity_base\":" << snapshot.entityScanDetail.selfVelocityBase
        << ",\"self_bone_base\":" << snapshot.entityScanDetail.selfBoneBase
        << ",\"self_playable\":" << snapshot.entityScanDetail.selfPlayable
        << ",\"dynamic_pairs\":" << snapshot.entityScanDetail.dynamicPairs
        << ",\"total_pairs\":" << snapshot.entityScanDetail.totalPairs
        << ",\"sample_reject\":{\"reason\":" << snapshot.entityScanDetail.sampleRejectReason
        << ",\"parent\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectParent);
    out << ",\"match_id\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectMatchId);
    out << ",\"health_base\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectHealthBase);
    out << ",\"hero_base\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectHeroBase);
    out << ",\"hero_id\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectHeroId);
    out << ",\"velocity_base\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectVelocityBase);
    out << ",\"bone_base\":";
    AppendHexOrNull(out, snapshot.entityScanDetail.sampleRejectBoneBase);
    out << ",\"health_cm\":" << snapshot.entityScanDetail.sampleRejectHealthCm
        << ",\"health_max_cm\":" << snapshot.entityScanDetail.sampleRejectHealthMaxCm
        << "}},\"render\":{\"draw_radar_called\":" << (snapshot.renderDrawRadarCalled ? "true" : "false")
        << ",\"player_info_called\":" << (snapshot.renderPlayerInfoCalled ? "true" : "false")
        << ",\"skill_info_called\":" << (snapshot.renderSkillInfoCalled ? "true" : "false")
        << ",\"entity_list_empty\":" << (snapshot.renderEntityListEmpty ? "true" : "false")
        << "},\"player_info\":{\"input\":" << snapshot.playerInfo.input
        << ",\"projected\":" << snapshot.playerInfo.projected
        << ",\"drawn\":" << snapshot.playerInfo.drawn
        << ",\"skipped_dead\":" << snapshot.playerInfo.skippedDead
        << ",\"skipped_local_health\":" << snapshot.playerInfo.skippedLocalHealth
        << ",\"skipped_local_entity\":" << snapshot.playerInfo.skippedLocalEntity
        << ",\"skipped_distance\":" << snapshot.playerInfo.skippedDistance
        << ",\"skipped_opacity\":" << snapshot.playerInfo.skippedOpacity
        << ",\"skipped_world_to_screen\":" << snapshot.playerInfo.skippedWorldToScreen
        << ",\"skipped_world_to_screen_low\":" << snapshot.playerInfo.skippedWorldToScreenLow
        << ",\"skipped_world_to_screen_high\":" << snapshot.playerInfo.skippedWorldToScreenHigh
        << ",\"skipped_box\":" << snapshot.playerInfo.skippedBox
        << ",\"skipped_window\":" << snapshot.playerInfo.skippedWindow
        << ",\"sample_projected\":{\"available\":" << (snapshot.playerInfo.sampleProjected ? "true" : "false")
        << ",\"address\":";
    AppendHexOrNull(out, snapshot.playerInfo.sampleProjectedAddress);
    out << ",\"hero_id\":";
    AppendHexOrNull(out, snapshot.playerInfo.sampleProjectedHeroId);
    out << ",\"left\":" << snapshot.playerInfo.sampleProjectedLeft
        << ",\"top\":" << snapshot.playerInfo.sampleProjectedTop
        << ",\"width\":" << snapshot.playerInfo.sampleProjectedWidth
        << ",\"height\":" << snapshot.playerInfo.sampleProjectedHeight
        << ",\"center_x\":" << snapshot.playerInfo.sampleProjectedCenterX
        << ",\"bottom\":" << snapshot.playerInfo.sampleProjectedBottom
        << ",\"distance_m\":" << snapshot.playerInfo.sampleProjectedDistanceM
        << "},\"sample_drawn\":{\"available\":" << (snapshot.playerInfo.sampleDrawn ? "true" : "false")
        << ",\"address\":";
    AppendHexOrNull(out, snapshot.playerInfo.sampleDrawnAddress);
    out << ",\"hero_id\":";
    AppendHexOrNull(out, snapshot.playerInfo.sampleDrawnHeroId);
    out << ",\"left\":" << snapshot.playerInfo.sampleDrawnLeft
        << ",\"top\":" << snapshot.playerInfo.sampleDrawnTop
        << ",\"width\":" << snapshot.playerInfo.sampleDrawnWidth
        << ",\"height\":" << snapshot.playerInfo.sampleDrawnHeight
        << ",\"center_x\":" << snapshot.playerInfo.sampleDrawnCenterX
        << ",\"bottom\":" << snapshot.playerInfo.sampleDrawnBottom
        << ",\"distance_m\":" << snapshot.playerInfo.sampleDrawnDistanceM
        << "}},\"entity_process\":{\"raw\":" << snapshot.entityProcess.raw
        << ",\"validated\":" << snapshot.entityProcess.validated
        << ",\"dynamic\":" << snapshot.entityProcess.dynamic
        << ",\"null_pair\":" << snapshot.entityProcess.nullPair
        << ",\"duplicate\":" << snapshot.entityProcess.duplicate
        << ",\"health_base_fail\":" << snapshot.entityProcess.healthBaseFail
        << ",\"health_base_missing\":" << snapshot.entityProcess.healthBaseMissing
        << ",\"health_read_fail\":" << snapshot.entityProcess.healthReadFail
        << ",\"link_base_fail\":" << snapshot.entityProcess.linkBaseFail
        << ",\"hero_base_missing\":" << snapshot.entityProcess.heroBaseMissing
        << ",\"hero_fallback_fail\":" << snapshot.entityProcess.heroFallbackFail
        << ",\"name_unknown\":" << snapshot.entityProcess.nameUnknown
        << ",\"bone_candidates\":" << snapshot.entityProcess.boneCandidates
        << ",\"bone_base_nonzero\":" << snapshot.entityProcess.boneBaseNonZero
        << ",\"velocity_bone_data_nonzero\":" << snapshot.entityProcess.velocityBoneDataNonZero
        << ",\"bone_data_ptr_nonzero\":" << snapshot.entityProcess.boneDataPtrNonZero
        << ",\"bones_base_nonzero\":" << snapshot.entityProcess.bonesBaseNonZero
        << ",\"velocity_bone_id_table_nonzero\":" << snapshot.entityProcess.velocityBoneIdTableNonZero
        << ",\"velocity_bone_count_valid\":" << snapshot.entityProcess.velocityBoneCountValid
        << ",\"velocity_bone_id_table_readable\":" << snapshot.entityProcess.velocityBoneIdTableReadable
        << ",\"velocity_bone_head_id_found\":" << snapshot.entityProcess.velocityBoneHeadIdFound
        << ",\"skeleton_any_valid\":" << snapshot.entityProcess.skeletonAnyValid
        << ",\"skeleton_head_valid\":" << snapshot.entityProcess.skeletonHeadValid
        << ",\"head_probe_candidates\":" << snapshot.entityProcess.headProbeCandidates
        << ",\"head_probe_resolved\":" << snapshot.entityProcess.headProbeResolved
        << ",\"head_probe_id_found\":" << snapshot.entityProcess.headProbeIdFound
        << ",\"head_probe_local_finite\":" << snapshot.entityProcess.headProbeLocalFinite
        << ",\"head_probe_local_nonzero\":" << snapshot.entityProcess.headProbeLocalNonZero
        << ",\"head_probe_world_nonzero\":" << snapshot.entityProcess.headProbeWorldNonZero
        << ",\"head_probe_exceptions\":" << snapshot.entityProcess.headProbeExceptions
        << ",\"head_probe_near_candidates\":" << snapshot.entityProcess.headProbeNearCandidates
        << ",\"head_probe_near_world_nonzero\":" << snapshot.entityProcess.headProbeNearWorldNonZero
        << ",\"head_probe_far_candidates\":" << snapshot.entityProcess.headProbeFarCandidates
        << ",\"head_probe_far_world_nonzero\":" << snapshot.entityProcess.headProbeFarWorldNonZero
        << ",\"sample_bone_address\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleBoneAddress);
    out << ",\"sample_health_fail_component_parent\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailComponentParent);
    out << ",\"sample_health_fail_link_parent\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailLinkParent);
    out << ",\"sample_health_fail_health_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailHealthBase);
    out << ",\"sample_health_fail_link_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailLinkBase);
    out << ",\"sample_health_fail_velocity_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailVelocityBase);
    out << ",\"sample_health_fail_hero_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailHeroBase);
    out << ",\"sample_health_fail_team_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailTeamBase);
    out << ",\"sample_health_fail_bone_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleHealthFailBoneBase);
    out << ",\"sample_health_fail_read_ok\":" << snapshot.entityProcess.sampleHealthFailReadOk
        << ",\"sample_name_unknown_component_parent\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownComponentParent);
    out << ",\"sample_name_unknown_link_parent\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownLinkParent);
    out << ",\"sample_name_unknown_component_match_id\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownComponentMatchId);
    out << ",\"sample_name_unknown_link_match_id\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownLinkMatchId);
    out << ",\"sample_name_unknown_hero_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownHeroBase);
    out << ",\"sample_name_unknown_hero_id\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownHeroId);
    out << ",\"sample_name_unknown_hero_id_candidate\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownHeroIdCandidate);
    out << ",\"sample_name_unknown_hero_id_candidate_offset\":" << snapshot.entityProcess.sampleNameUnknownHeroIdCandidateOffset;
    out << ",\"sample_name_unknown_component_hero_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownComponentHeroBase);
    out << ",\"sample_name_unknown_component_hero_id\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownComponentHeroId);
    out << ",\"sample_name_unknown_component_hero_id_candidate\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownComponentHeroIdCandidate);
    out << ",\"sample_name_unknown_component_hero_id_candidate_offset\":" << snapshot.entityProcess.sampleNameUnknownComponentHeroIdCandidateOffset;
    out << ",\"sample_name_unknown_link_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownLinkBase);
    out << ",\"sample_name_unknown_skill_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownSkillBase);
    out << ",\"sample_name_unknown_team_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownTeamBase);
    out << ",\"sample_name_unknown_bone_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleNameUnknownBoneBase);
    out << ",\"sample_name_unknown_kind\":" << snapshot.entityProcess.sampleNameUnknownKind
        << ",\"sample_velocity_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleVelocityBase);
    out << ",\"sample_bone_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleBoneBase);
    out << ",\"sample_velocity_bone_data\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleVelocityBoneData);
    out << ",\"sample_bone_data_ptr\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleBoneDataPtr);
    out << ",\"sample_bones_base\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleBonesBase);
    out << ",\"sample_bone_id_table\":";
    AppendHexOrNull(out, snapshot.entityProcess.sampleBoneIdTable);
    out << ",\"sample_bone_count\":" << snapshot.entityProcess.sampleBoneCount
        << ",\"sample_bone_id_table_readable\":" << snapshot.entityProcess.sampleBoneIdTableReadable
        << ",\"sample_bone_head_index\":" << snapshot.entityProcess.sampleBoneHeadIndex
        << "},\"local_entity\":{\"selected\":" << snapshot.localEntity.selected
        << ",\"selected_health\":" << snapshot.localEntity.selectedHealth
        << ",\"best_distance_cm\":" << snapshot.localEntity.bestDistanceCm
        << "}}}";
    return out.str();
}

void AppendHeroPerkJson(std::ostringstream& out, const OW::HeroPerks::State& perk)
{
    const OW::HeroPerks::Classification& classification = perk.classification;
    const OW::HeroPerks::ResearchSelectedBoolean& researchSelected = perk.researchSelected;
    out << "{\"available\":" << (perk.available ? "true" : "false")
        << ",\"lookup_ready\":" << (perk.lookupReady ? "true" : "false")
        << ",\"policy\":";
    AppendJsonString(out, OW::HeroPerkLookupSeed::kPolicyName);
    out << ",\"seed_generated_at_utc\":";
    AppendJsonString(out, OW::HeroPerkLookupSeed::kGeneratedAtUtc);
    out << ",\"seed_trusted_rows\":" << OW::HeroPerkLookupSeed::kTrustedRows
        << ",\"seed_trusted_samples\":" << OW::HeroPerkLookupSeed::kTrustedSamples
        << ",\"seed_source_holdout_known\":" << OW::HeroPerkLookupSeed::kSourceHoldoutKnownSamples
        << ",\"seed_source_holdout_samples\":" << OW::HeroPerkLookupSeed::kSourceHoldoutSamples
        << ",\"seed_source_holdout_false_known\":" << OW::HeroPerkLookupSeed::kSourceHoldoutFalseKnown
        << ",\"skill_base\":";
    AppendHexOrNull(out, perk.skillBase);
    out << ",\"e44_read\":" << (perk.e44Read ? "true" : "false")
        << ",\"e44_u32\":";
    AppendHexString(out, perk.e44U32);
    out << ",\"e44_u64\":";
    AppendHexString(out, perk.e44U64);
    out << ",\"e78_read\":" << (perk.e78Read ? "true" : "false")
        << ",\"e78_u32\":";
    AppendHexString(out, perk.e78U32);
    out << ",\"e78_u64\":";
    AppendHexString(out, perk.e78U64);
    out << ",\"research_selected_boolean\":{"
        << "\"evidence\":\"20260615_live_validation_fail_closed\","
        << "\"supported_hero\":" << (researchSelected.supportedHero ? "true" : "false")
        << ",\"available\":" << (researchSelected.available ? "true" : "false")
        << ",\"selected\":";
    if (researchSelected.available)
        out << (researchSelected.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"rule\":";
    if (researchSelected.rule && researchSelected.rule[0] != '\0')
        AppendJsonString(out, researchSelected.rule);
    else
        out << "null";
    out << ",\"raw\":{"
        << "\"skill_16c2_read\":" << (researchSelected.skill16C2Read ? "true" : "false")
        << ",\"skill_16c2_u16\":";
    AppendHexString(out, researchSelected.skill16C2);
    out << ",\"symmetra_02e8_read\":" << (researchSelected.symmetra02E8Read ? "true" : "false")
        << ",\"symmetra_02e8_u64\":";
    AppendHexString(out, researchSelected.symmetra02E8);
    out << ",\"component55_base\":";
    AppendHexOrNull(out, researchSelected.component55Base);
    out << ",\"component55_270_read\":" << (researchSelected.component55_270Read ? "true" : "false")
        << ",\"component55_270_u32\":";
    AppendHexString(out, researchSelected.component55_270);
    out << "}}";
    out << ",\"selected_boolean\":";
    AppendMergedSelectedBooleanJson(out, perk.mergedSelected);
    out << ",\"statescript_ea0c_selected_boolean\":";
    AppendStateScriptEa0cSelectedBooleanJson(out, perk.stateScriptEa0cSelected);
    out << ",\"ana_headshot_selected_boolean\":";
    AppendAnaHeadshotSelectedBooleanJson(out, perk.anaHeadshotSelected);
    out << ",\"raw_selected_boolean\":";
    AppendRawSelectedBooleanJson(out, perk.rawSelected);
    out << ",\"candidate_slot_selected_boolean\":";
    AppendCandidateSlotSelectedBooleanJson(out, perk.candidateSlotSelected);
    out << ",\"candidate_slots\":";
    AppendCandidateSlotsJson(out, perk);
    out << ",\"result\":";
    AppendJsonString(out, OW::HeroPerks::ResultName(classification.result));
    out << ",\"selected\":";
    if (OW::HeroPerks::IsKnown(classification.result))
        out << (classification.selected ? "true" : "false");
    else
        out << "null";
    out << ",\"tier\":";
    if (classification.tier && classification.tier[0] != '\0')
        AppendJsonString(out, classification.tier);
    else
        out << "null";
    out << ",\"key\":";
    AppendHexOrNull(out, classification.key);
    out << ",\"signatures\":{"
        << "\"ordered\":";
    AppendHexString(out, perk.orderedSignature);
    out << ",\"cluster\":";
    AppendHexString(out, perk.clusterSignature);
    out << ",\"unique_no338\":";
    AppendHexString(out, perk.uniqueNo338Signature);
    out << ",\"multiset\":";
    AppendHexString(out, perk.multisetSignature);
    out << "},\"keys\":{"
        << "\"ordered\":";
    AppendHexString(out, perk.orderedKey);
    out << ",\"cluster\":";
    AppendHexString(out, perk.clusterKey);
    out << ",\"unique_no338\":";
    AppendHexString(out, perk.uniqueNo338Key);
    out << ",\"multiset\":";
    AppendHexString(out, perk.multisetKey);
    out << ",\"state_code\":";
    AppendHexString(out, perk.stateCodeKey);
    out << "},\"research_candidate_signatures\":{";
    for (size_t index = 0; index < perk.researchCandidateKeyCount; ++index) {
        if (index != 0)
            out << ',';
        const OW::HeroPerks::ResearchCandidateKey& candidate = perk.researchCandidateKeys[index];
        AppendJsonString(out, candidate.name ? candidate.name : "");
        out << ':';
        AppendHexString(out, candidate.signature);
    }
    out << "},\"research_candidate_keys\":{";
    for (size_t index = 0; index < perk.researchCandidateKeyCount; ++index) {
        if (index != 0)
            out << ',';
        const OW::HeroPerks::ResearchCandidateKey& candidate = perk.researchCandidateKeys[index];
        AppendJsonString(out, candidate.name ? candidate.name : "");
        out << ':';
        AppendHexString(out, candidate.key);
    }
    out << "},\"checked\":[";
    for (size_t index = 0; index < classification.checkedCount; ++index) {
        if (index != 0)
            out << ',';
        const OW::HeroPerks::CheckedTier& checked = classification.checked[index];
        out << "{\"tier\":";
        AppendJsonString(out, checked.tier ? checked.tier : "");
        out << ",\"key\":";
        AppendHexString(out, checked.key);
        out << ",\"hit\":" << (checked.hit ? "true" : "false")
            << ",\"collision\":" << (checked.collision ? "true" : "false")
            << '}';
    }
    out << "]}";
}

std::string BuildBzOffsetProbeJson()
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out);

    const uint64_t base =
        OW::SDK && OW::SDK->IsInitialized() ? OW::SDK->dwGameBase : 0;
    const auto& activeOffsets = OW::offset::Active();

    out << ",\"process\":{\"connected\":"
        << (OW::ProcessConnection::IsConnected() ? "true" : "false")
        << ",\"pid\":" << OW::ProcessConnection::ConnectedPid()
        << ",\"base_address\":";
    AppendHexOrNull(out, base);
    out << ",\"profile\":";
    AppendJsonString(out, activeOffsets.name);
    out << "},\"screen\":{\"manual_width\":"
        << OW::Config::manualScreenWidth
        << ",\"manual_height\":" << OW::Config::manualScreenHeight
        << ",\"resolved_wx\":";
    AppendNumberOrNull(out, OW::WX);
    out << ",\"resolved_wy\":";
    AppendNumberOrNull(out, OW::WY);
    out << "},\"input_mouse_scale\":{\"x\":";
    float inputMouseScaleX = 0.0f;
    const bool inputMouseScaleXRead =
        TryReadValue(base + activeOffsets.InputMouseScaleX_RVA, inputMouseScaleX);
    if (inputMouseScaleXRead)
        AppendNumberOrNull(out, inputMouseScaleX);
    else
        out << "null";
    out << ",\"y\":";
    float inputMouseScaleY = 0.0f;
    const bool inputMouseScaleYRead =
        TryReadValue(base + activeOffsets.InputMouseScaleY_RVA, inputMouseScaleY);
    if (inputMouseScaleYRead)
        AppendNumberOrNull(out, inputMouseScaleY);
    else
        out << "null";
    out << "},\"viewport_candidates\":[";
    AppendRvaDwordProbeJson(out, "active_width", base, activeOffsets.ViewportWidth_RVA);
    out << ',';
    AppendRvaDwordProbeJson(out, "active_height", base, activeOffsets.ViewportHeight_RVA);
    out << ',';
    AppendRvaDwordProbeJson(out, "bz150480_width_4019bc8", base, 0x4019BC8);
    out << ',';
    AppendRvaDwordProbeJson(out, "bz150480_height_4019c38", base, 0x4019C38);
    out << ',';
    AppendRvaDwordProbeJson(out, "ne_input_delta_width_49b24f8", base, 0x49B24F8);
    out << ',';
    AppendRvaDwordProbeJson(out, "ne_input_delta_height_49b2568", base, 0x49B2568);
    out << "],\"entity_roots\":[";
    AppendRvaQwordProbeJson(out, "active_ida_390d7c8", base, OW::offset::Address_entity_base);
    out << ',';
    AppendRvaDwordProbeJson(out, "active_slot_count_390d7b4", base, OW::offset::EntityList_SlotCount_RVA);
    out << ',';
    AppendRvaQwordProbeJson(out, "forum_betsit_39017b8", base, 0x39017B8);
    out << "],\"global_admin\":[";
    AppendSingletonProbeJson(
        out,
        "active_configured_root",
        base,
        OW::offset::GlobalAdmin_WorldBz_RVA,
        OW::offset::Singleton_InputOffset,
        false);
    out << ',';
    AppendSingletonProbeJson(
        out,
        "ida_global_admin_3a71930",
        base,
        0x3A71930,
        OW::offset::Singleton_InputOffset,
        false);
    out << ',';
    AppendSingletonProbeJson(
        out,
        "forum_betsit_3a65930",
        base,
        0x3A65930,
        OW::offset::Singleton_InputOffset,
        false);
    out << ',';
    AppendSingletonProbeJson(
        out,
        "forum_client_game_3a5fbc8_liveadmin",
        base,
        0x3A5FBC8,
        0x30,
        true);
    uint64_t liveGameAdmin = 0;
    const bool liveGameAdminOk = OW::TryGetLiveGameAdmin(liveGameAdmin);
    uint64_t liveClientGame = 0;
    TryReadValue(base + 0x3A5FBC8, liveClientGame);
    uint64_t idaGlobalAdmin = 0;
    TryReadValue(base + 0x3A71930, idaGlobalAdmin);
    uint64_t forumGlobalAdmin = 0;
    TryReadValue(base + 0x3A65930, forumGlobalAdmin);
    float liveSensitivity = 0.0f;
    uint64_t liveSensitivitySource = 0;
    const bool liveSensitivityOk =
        OW::TryReadLiveGameAdminSensitivity(liveSensitivity, &liveSensitivitySource);
    float autoSensitivity = 0.0f;
    uint64_t autoSensitivitySource = 0;
    const bool autoSensitivityOk =
        OW::TryReadGameMouseSensitivity(autoSensitivity, &autoSensitivitySource);
    out << "],\"live_game_admin\":{\"read\":"
        << (liveGameAdminOk ? "true" : "false")
        << ",\"address\":";
    AppendHexOrNull(out, liveGameAdmin);
    out << ",\"sensitivity_read\":"
        << (liveSensitivityOk ? "true" : "false")
        << ",\"sensitivity\":";
    if (liveSensitivityOk)
        AppendNumberOrNull(out, liveSensitivity);
    else
        out << "null";
    out << ",\"sensitivity_source\":";
    AppendHexOrNull(out, liveSensitivitySource);
    out << ",\"auto_sensitivity_read\":"
        << (autoSensitivityOk ? "true" : "false")
        << ",\"auto_sensitivity\":";
    if (autoSensitivityOk)
        AppendNumberOrNull(out, autoSensitivity);
    else
        out << "null";
    out << ",\"auto_sensitivity_source\":";
    AppendHexOrNull(out, autoSensitivitySource);
    out << "},\"float_scans\":[";
    AppendFloatScanJson(out, "client_game", liveClientGame, 0x8000, 512);
    out << ',';
    AppendFloatScanJson(out, "live_game_admin", liveGameAdmin, 0x8000, 512);
    out << ',';
    AppendFloatScanJson(out, "forum_global_admin", forumGlobalAdmin, 0x8000, 512);
    out << "],\"target_float_scans\":[";
    AppendFloatTargetScanJson(out, "client_game_4_5", liveClientGame, 0x40000, 4.5f, 0.002f, 64);
    out << ',';
    AppendFloatTargetScanJson(out, "live_game_admin_4_5", liveGameAdmin, 0x40000, 4.5f, 0.002f, 64);
    out << ',';
    AppendFloatTargetScanJson(out, "forum_global_admin_4_5", forumGlobalAdmin, 0x40000, 4.5f, 0.002f, 64);
    out << "],\"sensitivity_structure\":";
    AppendBzSensitivityStructureJson(out, liveClientGame, liveGameAdmin, forumGlobalAdmin);
    out << ",\"input_options_static_probe\":";
    AppendBzInputOptionsStaticProbeJson(out, base, liveGameAdmin);
    out << ",\"component_material\":[";
    AppendComponentMaterialProbeJson(
        out,
        "active_ida_component",
        base,
        OW::offset::ComponentXorQword_RVA,
        OW::offset::ComponentXorQword_Off,
        OW::offset::ComponentXorByte_RVA);
    out << ',';
    AppendComponentMaterialProbeJson(
        out,
        "active_prexor_doc",
        base,
        OW::offset::ComponentXorQword_RVA,
        OW::offset::ComponentXorQword_Off,
        OW::offset::ComponentPreXorByte_RVA);
    out << ',';
    AppendComponentMaterialProbeJson(
        out,
        "forum_sujung_component",
        base,
        0x3A5FAB0,
        0x10C,
        0x3746789);
    out << ',';
    AppendComponentMaterialProbeJson(
        out,
        "forum_sujung_visibility",
        base,
        0x3A5FAB0,
        106,
        0x3746659);
    out << "],\"viewmatrix_roots\":[";
    AppendViewMatrixRootProbeJson(
        out,
        "active_bz_38a6980",
        base,
        OW::offset::Address_viewmatrix_base,
        OW::offset::offset_viewmatrix_xor_key,
        OW::offset::offset_viewmatrix_xor_key2,
        OW::offset::offset_viewmatrix_xor_key3,
        "sub_xor_sub");
    out << ',';
    AppendViewMatrixRootProbeJson(
        out,
        "ida_direct_38b2a88",
        base,
        OW::offset::Address_viewmatrix_direct_base,
        OW::offset::offset_viewmatrix_xor_key,
        OW::offset::offset_viewmatrix_xor_key2,
        0,
        "add_xor");
    out << ',';
    AppendViewMatrixRootProbeJson(
        out,
        "ida_adjacent_38b2aa0",
        base,
        OW::offset::Address_viewmatrix_primary_base,
        OW::offset::offset_viewmatrix_xor_key,
        OW::offset::offset_viewmatrix_xor_key2,
        0,
        "add_xor");
    out << ',';
    AppendViewMatrixRootProbeJson(
        out,
        "forum_betsit_38a6980",
        base,
        0x38A6980,
        0x59D406B75C2A4377ull,
        0xD54D81BA4EED36CEull,
        0x1C840F09D6923D76ull,
        "sub_xor_sub");
    out << "]}";
    return out.str();
}

std::string BuildLocalJson()
{
    std::vector<std::string> warnings;
    const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
    const OW::TargetingDetail::FovRuntimeContext fov = OW::TargetingDetail::SnapshotFovRuntimeContext();
    const OW::HeroPerks::State perk =
        OW::HeroPerks::ReadCurrent(local.HeroID, local.SkillBase, local.address);
    const bool hasLocal = local.address != 0 || local.HeroID != 0 || local.LinkBase != 0;
    if (!hasLocal)
        warnings.emplace_back("local entity snapshot is unavailable");
    if (!fov.valid)
        warnings.emplace_back("camera/view direction is unavailable");

    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out, warnings);
    out << ",\"local\":{";
    out << "\"hero_id\":";
    if (local.HeroID != 0)
        AppendHexString(out, local.HeroID);
    else
        out << "null";
    out << ",\"hero_name\":";
    const std::string heroName = HeroName(local.HeroID, local.LinkBase);
    if (!heroName.empty())
        AppendJsonString(out, heroName);
    else
        out << "null";
    out << ",\"health\":";
    if (hasLocal)
        AppendNumberOrNull(out, local.PlayerHealth);
    else
        out << "null";
    out << ",\"health_max\":";
    if (hasLocal)
        AppendNumberOrNull(out, local.PlayerHealthMax);
    else
        out << "null";
    out << ",\"alive\":";
    if (hasLocal)
        out << (local.Alive ? "true" : "false");
    else
        out << "null";
    out << ",\"position\":";
    if (hasLocal)
        AppendVectorOrNull(out, local.pos);
    else
        out << "null";
    out << ",\"camera_position\":";
    if (fov.valid)
        AppendVectorOrNull(out, fov.camera);
    else
        out << "null";
    out << ",\"view_direction\":";
    if (fov.valid)
        AppendVectorOrNull(out, fov.forward);
    else
        out << "null";
    out << ",\"link_base\":";
    AppendHexOrNull(out, local.LinkBase);
    out << ",\"skill_base\":";
    AppendHexOrNull(out, local.SkillBase);
    out << ",\"player_controller\":";
    AppendHexOrNull(out, OW::SDK ? OW::SDK->g_player_controller : 0);
    out << ",\"ultimate_perk\":";
    AppendHeroPerkJson(out, perk);
    out << "}}";
    return out.str();
}

std::string BuildEntitiesJson(const std::unordered_map<std::string, std::string>& rawQuery)
{
    EntityQuery query = ParseEntityQuery(rawQuery);
    const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
    std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();

    struct Candidate {
        OW::c_entity entity{};
        float distanceM = 0.0f;
        uint64_t key = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(entities.size());
    for (const OW::c_entity& entity : entities) {
        const bool alive = EntityFreshAndAlive(entity);
        if (!query.includeDead && !alive)
            continue;
        if (!TeamMatches(entity, query.team))
            continue;

        const float distance = IsFiniteVector(entity.pos) && IsFiniteVector(local.pos)
            ? entity.pos.DistTo(local.pos)
            : (std::numeric_limits<float>::max)();
        if (distance > query.maxDistanceM)
            continue;

        candidates.push_back(Candidate{ entity, distance, EntityKey(entity) });
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            const bool lhsAlive = EntityFreshAndAlive(lhs.entity);
            const bool rhsAlive = EntityFreshAndAlive(rhs.entity);
            if (lhsAlive != rhsAlive)
                return lhsAlive && !rhsAlive;
            if (lhs.distanceM != rhs.distanceM)
                return lhs.distanceM < rhs.distanceM;
            return lhs.key < rhs.key;
        });

    if (static_cast<int>(candidates.size()) > query.limit)
        candidates.resize(static_cast<size_t>(query.limit));

    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out, query.warnings);
    out << ",\"query\":{\"max_distance_m\":";
    AppendNumberOrNull(out, query.maxDistanceM);
    out << ",\"team\":";
    AppendJsonString(out, query.team);
    out << ",\"include_dead\":" << (query.includeDead ? "true" : "false")
        << ",\"limit\":" << query.limit
        << "},\"entities\":[";

    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0)
            out << ',';

        const OW::c_entity& entity = candidates[index].entity;
        const std::string heroName = HeroName(entity.HeroID, entity.LinkBase);
        const bool syntheticCnNeTrainingBot =
            OW::offset::IsCnNeProfile() &&
            !entity.HeroBase &&
            OW::GameData::IsTrainingBotHeroId(entity.HeroID);
        out << "{\"key\":";
        AppendHexString(out, candidates[index].key);
        out << ",\"address\":";
        AppendHexString(out, entity.address);
        out << ",\"component_parent\":";
        AppendHexOrNull(out, entity.address);
        out << ",\"link_parent\":";
        AppendHexOrNull(out, entity.LinkParent);
        out << ",\"health_base\":";
        AppendHexOrNull(out, entity.HealthBase);
        out << ",\"link_base\":";
        AppendHexOrNull(out, entity.LinkBase);
        out << ",\"team_base\":";
        AppendHexOrNull(out, entity.TeamBase);
        out << ",\"velocity_base\":";
        AppendHexOrNull(out, entity.VelocityBase);
        out << ",\"hero_base\":";
        AppendHexOrNull(out, entity.HeroBase);
        out << ",\"bone_base\":";
        AppendHexOrNull(out, entity.BoneBase);
        out << ",\"skill_base\":";
        AppendHexOrNull(out, entity.SkillBase);
        out << ",\"visibility_base\":";
        AppendHexOrNull(out, entity.VisBase);
        out << ",\"player_controller_base\":";
        AppendHexOrNull(out, entity.AngleBase);
        out << ",\"enemy_angle_base\":";
        AppendHexOrNull(out, entity.EnemyAngleBase);
        out << ",\"hero_id\":";
        AppendHexString(out, entity.HeroID);
        out << ",\"hero_source\":";
        AppendJsonString(out, entity.HeroBase ? "component" : (syntheticCnNeTrainingBot ? "health_fallback" : "missing"));
        out << ",\"hero_name\":";
        AppendJsonString(out, heroName.empty() ? "Unknown" : heroName);
        out << ",\"team\":";
        AppendJsonString(out, entity.Team ? "enemy" : "ally");
        if (query.teamDebug) {
            out << ",\"team_debug\":";
            AppendTeamDebugJson(out, entity.TeamBase);
        }
        out << ",\"alive\":" << (entity.Alive ? "true" : "false")
            << ",\"visible\":" << (entity.Vis ? "true" : "false")
            << ",\"visibility_source\":";
        AppendJsonString(out, entity.VisBase ? "component" : (syntheticCnNeTrainingBot ? "synthetic_training_bot" : "missing"));
        out << ",\"health\":";
        AppendNumberOrNull(out, entity.PlayerHealth);
        out << ",\"health_max\":";
        AppendNumberOrNull(out, entity.PlayerHealthMax);
        out << ",\"distance_m\":";
        AppendNumberOrNull(out, candidates[index].distanceM);
        out << ",\"position\":";
        AppendVectorOrNull(out, entity.pos);
        out << ",\"head_position\":";
        AppendVectorOrNull(out, entity.head_pos);
        out << ",\"chest_position\":";
        AppendVectorOrNull(out, entity.chest_pos);
        const OW::EntityMotionState motion = OW::TargetingDetail::EstimateMotionState(entity, OW::Vector2{});
        const float groundedBaselineY = DiagnosticGroundedBaselineY(candidates[index].key, entity, motion);
        out << ",\"velocity\":";
        AppendVectorOrNull(out, motion.worldVelocity);
        out << ",\"motion_state\":";
        AppendJsonString(out, MotionKindName(motion.kind));
        out << ",\"motion_confidence\":";
        AppendNumberOrNull(out, motion.confidence);
        out << ",\"grounded_y_baseline\":";
        AppendNumberOrNull(out, groundedBaselineY);
        out << ",\"height_above_ground\":";
        AppendNumberOrNull(out,
            IsFiniteVector(entity.pos) && std::isfinite(groundedBaselineY)
                ? entity.pos.Y - groundedBaselineY
                : (std::numeric_limits<float>::quiet_NaN)());
        out << ",\"vertical_velocity_y\":";
        AppendNumberOrNull(out, motion.verticalVelocity);
        out << ",\"roster_state\":";
        AppendJsonString(out, RosterStateName(entity.roster_state));
        out << '}';
    }

    out << "]}";
    return out.str();
}

std::string BuildProjectionDebugJson(const std::unordered_map<std::string, std::string>& rawQuery)
{
    EntityQuery query = ParseEntityQuery(rawQuery);
    query.includeDead = true;

    const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
    std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();
    const float fallbackWidth = OW::Config::manualScreenWidth > 0
        ? static_cast<float>(OW::Config::manualScreenWidth)
        : static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    const float fallbackHeight = OW::Config::manualScreenHeight > 0
        ? static_cast<float>(OW::Config::manualScreenHeight)
        : static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    const OW::Vector2 window(OW::WX > 0.0f ? OW::WX : fallbackWidth,
        OW::WY > 0.0f ? OW::WY : fallbackHeight);

    OW::Matrix published{}, publishedCamera{};
    OW::SnapshotViewMatrices(published, publishedCamera);
    const bool publishedValid = MatrixNonIdentity(published);
    const bool publishedCameraValid = CameraViewMatrixPlausible(publishedCamera);
    const DirectX::XMFLOAT3 cameraPosRaw = publishedCamera.get_location();
    const DirectX::XMFLOAT3 cameraForwardRaw = publishedCamera.get_rotation();
    const OW::Vector3 cameraPos(cameraPosRaw.x, cameraPosRaw.y, cameraPosRaw.z);
    const OW::Vector3 cameraForward(cameraForwardRaw.x, cameraForwardRaw.y, cameraForwardRaw.z);

    const uint64_t moduleBase = OW::SDK && OW::SDK->IsInitialized() ? OW::SDK->dwGameBase : 0;
    const auto& activeOffsets = OW::offset::Active();
    const auto viewMatrixMode = activeOffsets.viewMatrixMode;
    const bool directRoot = viewMatrixMode == OW::offset::ViewMatrixMode::DirectChain;
    const bool subXorSubRoot = viewMatrixMode == OW::offset::ViewMatrixMode::EncryptedChainSubXorSub;

    uint64_t root = 0;
    uint64_t decoded = 0;
    uint64_t p1 = 0;
    uint64_t p2 = 0;
    uint64_t p3 = 0;
    uint64_t p4 = 0;
    uint64_t directRenderPtr = 0;
    uint64_t cameraPtr = 0;
    uint64_t projectionPtr = 0;
    OW::Matrix directRender{};
    OW::Matrix cameraView{};
    OW::Matrix projection{};
    bool directRead = false;
    bool cameraRead = false;
    bool projectionRead = false;

    if (moduleBase != 0 && activeOffsets.Address_viewmatrix_base != 0 &&
        TryReadValue(moduleBase + activeOffsets.Address_viewmatrix_base, root) && root != 0) {
        decoded = directRoot
            ? root
            : (subXorSubRoot
                ? ((root - activeOffsets.offset_viewmatrix_xor_key)
                    ^ activeOffsets.offset_viewmatrix_xor_key2) -
                    activeOffsets.offset_viewmatrix_xor_key3
                : ((root + activeOffsets.offset_viewmatrix_xor_key)
                    ^ activeOffsets.offset_viewmatrix_xor_key2));

        if (decoded != 0) {
            if (viewMatrixMode == OW::offset::ViewMatrixMode::EncryptedDirectMatrix) {
                directRenderPtr = decoded + activeOffsets.VM_DirectMatrix;
                cameraPtr = activeOffsets.VM_ViewMatrix ? decoded + activeOffsets.VM_ViewMatrix : 0;
            } else {
                TryReadValue(decoded + activeOffsets.VM_P1, p1);
                if (p1 != 0)
                    TryReadValue(p1 + activeOffsets.VM_P2, p2);
                if (p2 != 0) {
                    TryReadValue(p2 + activeOffsets.VM_ViewProjectionParent, p3);
                    if (p3 != 0)
                        TryReadValue(p3 + activeOffsets.VM_ViewProjectionPtr, p4);
                    if (p4 != 0)
                        directRenderPtr = p4 + activeOffsets.VM_ViewProjectionMatrix;
                    cameraPtr = p2 + activeOffsets.VM_ViewMatrix;
                    projectionPtr = p2 + activeOffsets.VM_ProjMatrix;
                }
            }
        }
    }

    if (directRenderPtr != 0)
        directRead = TryReadValue(directRenderPtr, directRender);
    if (cameraPtr != 0)
        cameraRead = TryReadValue(cameraPtr, cameraView);
    if (projectionPtr != 0)
        projectionRead = TryReadValue(projectionPtr, projection);

    const bool directValid = directRead && MatrixNonIdentity(directRender);
    const bool cameraValid = cameraRead && CameraViewMatrixPlausible(cameraView);
    const bool projectionValid = projectionRead && MatrixNonIdentity(projection);
    const bool composedRead = cameraRead && projectionRead;
    const OW::Matrix cameraProjection = composedRead ? OW::ComposeCameraProjection(cameraView, projection) : OW::Matrix{};
    const OW::Matrix projectionCamera = composedRead ? MultiplyMatricesLocal(projection, cameraView) : OW::Matrix{};
    const bool cameraProjectionValid = composedRead && MatrixNonIdentity(cameraProjection);
    const bool projectionCameraValid = composedRead && MatrixNonIdentity(projectionCamera);

    struct Candidate {
        OW::c_entity entity{};
        float distanceM = 0.0f;
        uint64_t key = 0;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(entities.size());
    for (const OW::c_entity& entity : entities) {
        if (!TeamMatches(entity, query.team))
            continue;
        const float distance = IsFiniteVector(entity.pos) && IsFiniteVector(local.pos)
            ? entity.pos.DistTo(local.pos)
            : (std::numeric_limits<float>::max)();
        if (distance > query.maxDistanceM)
            continue;
        candidates.push_back(Candidate{ entity, distance, EntityKey(entity) });
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.distanceM != rhs.distanceM)
                return lhs.distanceM < rhs.distanceM;
            return lhs.key < rhs.key;
        });
    if (static_cast<int>(candidates.size()) > query.limit)
        candidates.resize(static_cast<size_t>(query.limit));

    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out, query.warnings);
    out << ",\"query\":{\"max_distance_m\":";
    AppendNumberOrNull(out, query.maxDistanceM);
    out << ",\"team\":";
    AppendJsonString(out, query.team);
    out << ",\"limit\":" << query.limit
        << "},\"screen\":{\"width\":";
    AppendNumberOrNull(out, window.X);
    out << ",\"height\":";
    AppendNumberOrNull(out, window.Y);
    out << "},\"matrix_chain\":{\"profile\":";
    AppendJsonString(out, activeOffsets.name);
    out << ",\"mode\":" << static_cast<int>(viewMatrixMode)
        << ",\"module_base\":";
    AppendHexOrNull(out, moduleBase);
    out << ",\"root_rva\":";
    AppendHexString(out, activeOffsets.Address_viewmatrix_base);
    out << ",\"root\":";
    AppendHexOrNull(out, root);
    out << ",\"decoded\":";
    AppendHexOrNull(out, decoded);
    out << ",\"p1\":";
    AppendHexOrNull(out, p1);
    out << ",\"p2\":";
    AppendHexOrNull(out, p2);
    out << ",\"p3\":";
    AppendHexOrNull(out, p3);
    out << ",\"p4\":";
    AppendHexOrNull(out, p4);
    out << ",\"direct_render_ptr\":";
    AppendHexOrNull(out, directRenderPtr);
    out << ",\"camera_ptr\":";
    AppendHexOrNull(out, cameraPtr);
    out << ",\"projection_ptr\":";
    AppendHexOrNull(out, projectionPtr);
    out << ",\"published_ptr\":";
    AppendHexOrNull(out, directRenderPtr);
    out << ",\"published_camera_ptr\":";
    AppendHexOrNull(out, cameraPtr);
    out << ",\"direct_read\":" << (directRead ? "true" : "false")
        << ",\"direct_valid\":" << (directValid ? "true" : "false")
        << ",\"camera_read\":" << (cameraRead ? "true" : "false")
        << ",\"camera_valid\":" << (cameraValid ? "true" : "false")
        << ",\"projection_read\":" << (projectionRead ? "true" : "false")
        << ",\"projection_valid\":" << (projectionValid ? "true" : "false")
        << ",\"camera_times_projection_valid\":" << (cameraProjectionValid ? "true" : "false")
        << ",\"projection_times_camera_valid\":" << (projectionCameraValid ? "true" : "false")
        << ",\"published_valid\":" << (publishedValid ? "true" : "false")
        << ",\"published_camera_valid\":" << (publishedCameraValid ? "true" : "false")
        << ",\"camera_position\":";
    AppendVectorOrNull(out, cameraPos);
    out << ",\"camera_forward\":";
    AppendVectorOrNull(out, cameraForward);
    out << ",\"projection_fov\":";
    AppendProjectionFovJson(out, projection, projectionRead, window);
    out << "},\"entities\":[";

    for (size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0)
            out << ',';
        const OW::c_entity& entity = candidates[index].entity;
        const std::string heroName = HeroName(entity.HeroID, entity.LinkBase);
        const OW::Vector3 rootPos = entity.pos;
        const OW::Vector3 headPos = entity.head_pos;
        const OW::Vector3 chestPos = entity.chest_pos;
        const float forwardDotRoot = IsFiniteVector(rootPos) && IsFiniteVector(cameraPos) && IsFiniteVector(cameraForward)
            ? Dot(rootPos - cameraPos, cameraForward)
            : std::numeric_limits<float>::quiet_NaN();
        const float forwardDotChest = IsFiniteVector(chestPos) && IsFiniteVector(cameraPos) && IsFiniteVector(cameraForward)
            ? Dot(chestPos - cameraPos, cameraForward)
            : std::numeric_limits<float>::quiet_NaN();

        out << "{\"key\":";
        AppendHexString(out, candidates[index].key);
        out << ",\"address\":";
        AppendHexString(out, entity.address);
        out << ",\"hero_id\":";
        AppendHexString(out, entity.HeroID);
        out << ",\"hero_name\":";
        AppendJsonString(out, heroName.empty() ? "Unknown" : heroName);
        out << ",\"team\":";
        AppendJsonString(out, entity.Team ? "enemy" : "ally");
        out << ",\"alive\":" << (entity.Alive ? "true" : "false")
            << ",\"visible\":" << (entity.Vis ? "true" : "false")
            << ",\"health\":";
        AppendNumberOrNull(out, entity.PlayerHealth);
        out << ",\"health_max\":";
        AppendNumberOrNull(out, entity.PlayerHealthMax);
        out << ",\"distance_m\":";
        AppendNumberOrNull(out, candidates[index].distanceM);
        out << ",\"forward_dot_root\":";
        AppendNumberOrNull(out, forwardDotRoot);
        out << ",\"forward_dot_chest\":";
        AppendNumberOrNull(out, forwardDotChest);
        out << ",\"positions\":{\"root\":";
        AppendVectorOrNull(out, rootPos);
        out << ",\"head\":";
        AppendVectorOrNull(out, headPos);
        out << ",\"chest\":";
        AppendVectorOrNull(out, chestPos);
        out << "},\"projection\":{\"root\":";
        AppendProjectionSetJson(out,
            rootPos,
            window,
            published,
            publishedValid,
            directRender,
            directRead,
            directValid,
            cameraProjection,
            composedRead,
            cameraProjectionValid,
            projectionCamera,
            composedRead,
            projectionCameraValid);
        out << ",\"head\":";
        AppendProjectionSetJson(out,
            headPos,
            window,
            published,
            publishedValid,
            directRender,
            directRead,
            directValid,
            cameraProjection,
            composedRead,
            cameraProjectionValid,
            projectionCamera,
            composedRead,
            projectionCameraValid);
        out << ",\"chest\":";
        AppendProjectionSetJson(out,
            chestPos,
            window,
            published,
            publishedValid,
            directRender,
            directRead,
            directValid,
            cameraProjection,
            composedRead,
            cameraProjectionValid,
            projectionCamera,
            composedRead,
            projectionCameraValid);
        out << "}}";
    }

    out << "]}";
    return out.str();
}

std::optional<std::pair<size_t, OW::c_entity>> FindTargetEntity(
    const std::vector<OW::c_entity>& entities,
    const OW::TargetingDetail::TargetLockRuntime& lock)
{
    if (lock.active && lock.entityKey != 0) {
        for (size_t index = 0; index < entities.size(); ++index) {
            if (EntityKey(entities[index]) == lock.entityKey)
                return std::make_pair(index, entities[index]);
        }
    }

    if (OW::Config::Targetenemyi >= 0 &&
        static_cast<size_t>(OW::Config::Targetenemyi) < entities.size()) {
        return std::make_pair(static_cast<size_t>(OW::Config::Targetenemyi),
            entities[static_cast<size_t>(OW::Config::Targetenemyi)]);
    }

    if (lock.entityIndex >= 0 && static_cast<size_t>(lock.entityIndex) < entities.size())
        return std::make_pair(static_cast<size_t>(lock.entityIndex),
            entities[static_cast<size_t>(lock.entityIndex)]);

    return std::nullopt;
}

void AppendTargetObjectJson(std::ostringstream& out, std::vector<std::string>* warnings)
{
    std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();
    const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
    const OW::TargetingDetail::TargetLockRuntime lock =
        OW::TargetingDetail::SnapshotTargetLockRuntime();
    const OW::TargetingDetail::FovRuntimeContext fov = OW::TargetingDetail::SnapshotFovRuntimeContext();
    const OW::TargetCandidate candidate = OW::TargetingDetail::SnapshotLastTargetCandidate();
    const auto target = FindTargetEntity(entities, lock);
    if (warnings) {
        if (!target && candidate.valid)
            warnings->emplace_back("selected target uses last candidate snapshot because target lock is inactive");
        else if (!target)
            warnings->emplace_back("selected target snapshot is unavailable");
        if (!fov.valid)
            warnings->emplace_back("target angular error is unavailable");
    }

    out << "{\"selected_index\":";
    if (target)
        out << target->first;
    else if (lock.entityIndex >= 0)
        out << lock.entityIndex;
    else if (candidate.valid && candidate.entityIndex >= 0)
        out << candidate.entityIndex;
    else
        out << "null";
    out << ",\"selected_key\":";
    uint64_t selectedKey = target ? EntityKey(target->second) : lock.entityKey;
    if (selectedKey == 0 && candidate.valid)
        selectedKey = candidate.entityKey;
    AppendHexOrNull(out, selectedKey);
    out << ",\"lock_active\":" << (lock.active ? "true" : "false");

    if (target || candidate.valid) {
        const OW::c_entity& entity = target ? target->second : candidate.entitySnapshot;
        const bool candidateMatches =
            candidate.valid &&
            candidate.entityKey != 0 &&
            candidate.entityKey == selectedKey;
        OW::Vector3 aimPoint = entity.head_pos;
        if (!IsFiniteVector(aimPoint) || aimPoint == OW::Vector3(0, 0, 0))
            aimPoint = entity.chest_pos;
        if (!IsFiniteVector(aimPoint) || aimPoint == OW::Vector3(0, 0, 0))
            aimPoint = entity.pos;

        out << ",\"health\":";
        AppendNumberOrNull(out, entity.PlayerHealth);
        out << ",\"distance_m\":";
        const float distance = IsFiniteVector(entity.pos) && IsFiniteVector(local.pos)
            ? entity.pos.DistTo(local.pos)
            : (std::numeric_limits<float>::quiet_NaN)();
        AppendNumberOrNull(out, distance);
        out << ",\"aim_point\":";
        AppendVectorOrNull(out, aimPoint);
        out << ",\"angular_error_deg\":";
        if (fov.valid && IsFiniteVector(aimPoint))
            AppendNumberOrNull(out, OW::TargetingDetail::FovScoreDeg(fov, aimPoint));
        else
            out << "null";
        out << ",\"effective_fov_deg\":";
        if (candidateMatches)
            AppendNumberOrNull(out, candidate.effectiveFovDeg);
        else
            out << "null";
        out << ",\"dynamic_fov\":" << (candidateMatches && candidate.dynamicFov ? "true" : "false");
        out << ",\"dynamic_fov_preset_id\":";
        if (candidateMatches && candidate.dynamicFovPresetId >= 0)
            out << candidate.dynamicFovPresetId;
        else
            out << "null";
        out << ",\"candidate_fov_score_deg\":";
        if (candidateMatches)
            AppendNumberOrNull(out, candidate.fovScore);
        else
            out << "null";
        out << ",\"raw_aim_point\":";
        if (candidateMatches)
            AppendVectorOrNull(out, candidate.rawAimPoint);
        else
            out << "null";
        out << ",\"predicted_aim_point\":";
        if (candidateMatches)
            AppendVectorOrNull(out, candidate.predictedAimPoint);
        else
            out << "null";
        out << ",\"final_aim_point\":";
        if (candidateMatches)
            AppendVectorOrNull(out, candidate.aimPoint);
        else
            out << "null";

        const OW::WeaponSpec* weaponSpec = candidateMatches ? candidate.weaponSpec : nullptr;
        const OW::ProjectileRuntimeSpec projectile =
            OW::TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local, false);
        const bool predictionEnabled = candidateMatches &&
            OW::ResolvePredictionEnabled(
                OW::ClampPredictionOverride(OW::Config::aimbotPredictionMode),
                weaponSpec,
                OW::Config::Prediction);
        const OW::Motion::EntityMotionEstimate motion =
            candidateMatches
                ? OW::Motion::EstimateEntityMotion(candidate.entitySnapshot)
                : OW::Motion::EntityMotionEstimate{};
        const OW::Vector3 targetVelocity =
            candidateMatches
                ? OW::TargetingDetail::AccelerationAwareVelocity(
                    candidate.entitySnapshot,
                    candidate.distance,
                    projectile.projectileSpeed)
                : OW::Vector3{};
        const OW::LeadTimingEstimate timing =
            candidateMatches
                ? OW::TargetingDetail::EstimateLeadTimingForAimPoint(candidate.rawAimPoint, false)
                : OW::LeadTimingEstimate{};

        out << ",\"prediction_enabled\":";
        if (candidateMatches)
            out << (predictionEnabled ? "true" : "false");
        else
            out << "null";
        out << ",\"projectile_speed\":";
        if (candidateMatches)
            AppendNumberOrNull(out, projectile.projectileSpeed);
        else
            out << "null";
        out << ",\"gravity\":";
        if (candidateMatches)
            out << (projectile.gravity ? "true" : "false");
        else
            out << "null";
        out << ",\"motion_source\":";
        if (candidateMatches)
            AppendJsonString(out, OW::TargetingDetail::MotionVelocitySourceName(motion.velocitySource));
        else
            out << "null";
        out << ",\"motion_confidence\":";
        if (candidateMatches)
            AppendNumberOrNull(out, motion.confidence);
        else
            out << "null";
        out << ",\"target_velocity\":";
        if (candidateMatches)
            AppendVectorOrNull(out, targetVelocity);
        else
            out << "null";
        out << ",\"lead_timing_ms\":";
        if (candidateMatches) {
            out << "{\"settle\":";
            AppendNumberOrNull(out, timing.estimatedSettleMs);
            out << ",\"input\":" << timing.inputDelayMs
                << ",\"prefire\":";
            AppendNumberOrNull(out, timing.preFireDelayMs);
            out << '}';
        } else {
            out << "null";
        }
    } else {
        out << ",\"health\":null,\"distance_m\":null,\"aim_point\":null,\"angular_error_deg\":null"
            << ",\"raw_aim_point\":null,\"predicted_aim_point\":null,\"final_aim_point\":null"
            << ",\"prediction_enabled\":null,\"projectile_speed\":null,\"gravity\":null"
            << ",\"motion_source\":null,\"motion_confidence\":null,\"target_velocity\":null"
            << ",\"lead_timing_ms\":null";
    }

    out << '}';
}

std::string BuildTargetJson()
{
    std::vector<std::string> warnings;
    std::ostringstream targetOut;
    targetOut.imbue(std::locale::classic());
    AppendTargetObjectJson(targetOut, &warnings);

    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out, warnings);
    out << ",\"target\":" << targetOut.str() << '}';
    return out.str();
}

void AppendOutputMotionJson(std::ostringstream& out, uint64_t capturedAtMs)
{
    constexpr uint64_t kOutputMotionWindowMs = 120;
    const auto samples = OW::OutputMotionTelemetry::SnapshotSince(capturedAtMs, kOutputMotionWindowMs);

    out << "{\"available\":" << (!samples.empty() ? "true" : "false")
        << ",\"window_ms\":" << kOutputMotionWindowMs
        << ",\"sample_count\":" << samples.size();

    if (samples.empty()) {
        out << ",\"latest_sequence\":null"
            << ",\"latest_at_ms\":null"
            << ",\"latest_age_ms\":null"
            << ",\"delta_x\":null"
            << ",\"delta_y\":null"
            << ",\"delta_pitch_rad\":null"
            << ",\"delta_yaw_rad\":null"
            << ",\"pixel_x\":null"
            << ",\"pixel_y\":null"
            << ",\"automove_runtime_ms\":null"
            << ",\"status\":null"
            << ",\"split\":null"
            << ",\"steps\":null"
            << ",\"speed_deg_s\":null"
            << ",\"accel_deg_s2\":null"
            << ",\"jerk_deg_s3\":null"
            << ",\"max_speed_deg_s\":null"
            << ",\"max_accel_deg_s2\":null"
            << ",\"max_jerk_deg_s3\":null"
            << ",\"total_abs_delta_deg\":0"
            << ",\"speed_limit_deg_s\":null"
            << ",\"accel_limit_deg_s2\":null"
            << ",\"jerk_limit_deg_s3\":null"
            << ",\"violations\":[]"
            << '}';
        return;
    }

    constexpr float kRadToDeg = 57.29577951308232f;
    const auto& latest = samples.back();
    float maxSpeed = 0.0f;
    float maxAccel = 0.0f;
    float maxJerk = 0.0f;
    float totalAbsDeltaDeg = 0.0f;
    for (const auto& sample : samples) {
        maxSpeed = (std::max)(maxSpeed, std::fabs(sample.speedDegS));
        maxAccel = (std::max)(maxAccel, std::fabs(sample.accelDegS2));
        maxJerk = (std::max)(maxJerk, std::fabs(sample.jerkDegS3));
        totalAbsDeltaDeg += std::sqrt(
            sample.deltaPitchRad * sample.deltaPitchRad +
            sample.deltaYawRad * sample.deltaYawRad) * kRadToDeg;
    }

    out << ",\"latest_sequence\":" << latest.sequence
        << ",\"latest_at_ms\":" << latest.capturedAtMs
        << ",\"latest_age_ms\":" << (capturedAtMs >= latest.capturedAtMs ? capturedAtMs - latest.capturedAtMs : 0)
        << ",\"delta_x\":";
    AppendNumberOrNull(out, latest.deltaYawRad);
    out << ",\"delta_y\":";
    AppendNumberOrNull(out, latest.deltaPitchRad);
    out << ",\"delta_pitch_rad\":";
    AppendNumberOrNull(out, latest.deltaPitchRad);
    out << ",\"delta_yaw_rad\":";
    AppendNumberOrNull(out, latest.deltaYawRad);
    out << ",\"pixel_x\":" << latest.pixelX
        << ",\"pixel_y\":" << latest.pixelY
        << ",\"automove_runtime_ms\":" << latest.automoveRuntimeMs
        << ",\"status\":" << latest.status
        << ",\"split\":" << (latest.split ? "true" : "false")
        << ",\"steps\":" << latest.steps
        << ",\"speed_deg_s\":";
    AppendNumberOrNull(out, latest.speedDegS);
    out << ",\"accel_deg_s2\":";
    AppendNumberOrNull(out, latest.accelDegS2);
    out << ",\"jerk_deg_s3\":";
    AppendNumberOrNull(out, latest.jerkDegS3);
    out << ",\"max_speed_deg_s\":";
    AppendNumberOrNull(out, maxSpeed);
    out << ",\"max_accel_deg_s2\":";
    AppendNumberOrNull(out, maxAccel);
    out << ",\"max_jerk_deg_s3\":";
    AppendNumberOrNull(out, maxJerk);
    out << ",\"total_abs_delta_deg\":";
    AppendNumberOrNull(out, totalAbsDeltaDeg);
    out << ",\"speed_limit_deg_s\":null"
        << ",\"accel_limit_deg_s2\":null"
        << ",\"jerk_limit_deg_s3\":null"
        << ",\"violations\":[]"
        << '}';
}

void PushTargetHistorySample()
{
    std::ostringstream targetOut;
    targetOut.imbue(std::locale::classic());
    AppendTargetObjectJson(targetOut, nullptr);

    std::lock_guard<std::mutex> lock(g_targetHistoryMutex);
    TargetHistorySample sample{};
    sample.sequence = ++g_targetHistorySequence;
    sample.capturedAtMs = TimestampMs();
    sample.targetJson = targetOut.str();
    g_targetHistory.push_back(std::move(sample));
    if (g_targetHistory.size() > kTargetHistoryCapacity)
        g_targetHistory.erase(g_targetHistory.begin(),
            g_targetHistory.begin() + static_cast<std::ptrdiff_t>(g_targetHistory.size() - kTargetHistoryCapacity));
}

void TargetHistoryLoop(std::shared_ptr<std::atomic<bool>> runFlag)
{
    Diagnostics::Info("Unleashed target history sampler started at ~%d Hz.",
        1000 / kTargetHistoryPeriodMs);
    while (runFlag && runFlag->load(std::memory_order_acquire)) {
        const auto started = std::chrono::steady_clock::now();
        PushTargetHistorySample();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        const auto period = std::chrono::milliseconds(kTargetHistoryPeriodMs);
        if (elapsed < period)
            std::this_thread::sleep_for(period - elapsed);
    }
    Diagnostics::Info("Unleashed target history sampler stopped.");
}

std::string BuildTargetHistoryJson(const std::unordered_map<std::string, std::string>& rawQuery)
{
    std::vector<std::string> warnings;
    const int limit = std::clamp(ParseIntQuery(rawQuery, "limit", 256, warnings), 0, 1024);
    const int maxAgeMs = std::clamp(ParseIntQuery(rawQuery, "max_age_ms", 1500, warnings), 1, 60000);
    const uint64_t nowMs = TimestampMs();

    std::vector<TargetHistorySample> samples;
    {
        std::lock_guard<std::mutex> lock(g_targetHistoryMutex);
        samples.reserve(g_targetHistory.size());
        for (const TargetHistorySample& sample : g_targetHistory) {
            if (nowMs >= sample.capturedAtMs && nowMs - sample.capturedAtMs <= static_cast<uint64_t>(maxAgeMs))
                samples.push_back(sample);
        }
    }
    if (limit >= 0 && static_cast<int>(samples.size()) > limit) {
        samples.erase(samples.begin(),
            samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - static_cast<size_t>(limit)));
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());
    AppendCommonStart(out, warnings);
    out << ",\"server_time_ms\":" << nowMs
        << ",\"sample_hz\":";
    AppendNumberOrNull(out, 1000.0f / static_cast<float>(kTargetHistoryPeriodMs));
    out << ",\"capacity\":" << kTargetHistoryCapacity
        << ",\"query\":{\"limit\":" << limit
        << ",\"max_age_ms\":" << maxAgeMs
        << "},\"samples\":[";
    for (size_t index = 0; index < samples.size(); ++index) {
        if (index != 0)
            out << ',';
        const TargetHistorySample& sample = samples[index];
        out << "{\"sequence\":" << sample.sequence
            << ",\"captured_at_ms\":" << sample.capturedAtMs
            << ",\"target\":" << sample.targetJson
            << ",\"output_motion\":";
        AppendOutputMotionJson(out, sample.capturedAtMs);
        out << '}';
    }
    out << "]}";
    return out.str();
}

std::string BuildJsonForTarget(const RequestTarget& target, int& statusCode)
{
    statusCode = 200;
    if (target.path == "__method_not_allowed__") {
        statusCode = 405;
        std::ostringstream out;
        AppendErrorBody(out, statusCode, "Only GET is supported");
        return out.str();
    }
    if (target.path == "/api/health")
        return BuildHealthJson();
    if (target.path == "/api/diagnostics")
        return BuildDiagnosticsJson();
    if (target.path == "/api/bz-offset-probe")
        return BuildBzOffsetProbeJson();
    if (target.path == "/api/local")
        return BuildLocalJson();
    if (target.path == "/api/entities")
        return BuildEntitiesJson(target.query);
    if (target.path == "/api/projection-debug")
        return BuildProjectionDebugJson(target.query);
    if (target.path == "/api/target")
        return BuildTargetJson();
    if (target.path == "/api/target/history")
        return BuildTargetHistoryJson(target.query);

    statusCode = 404;
    std::ostringstream out;
    AppendErrorBody(out, statusCode, "Unknown endpoint");
    return out.str();
}

const char* StatusText(int statusCode)
{
    switch (statusCode) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    default: return "Internal Server Error";
    }
}

bool SendAll(SOCKET socketHandle, const std::string& data)
{
    const char* cursor = data.data();
    int remaining = static_cast<int>(data.size());
    while (remaining > 0) {
        const int sent = send(socketHandle, cursor, remaining, 0);
        if (sent <= 0)
            return false;
        cursor += sent;
        remaining -= sent;
    }
    return true;
}

void SendHttpResponse(SOCKET clientSocket, int statusCode, const std::string& body)
{
    std::ostringstream response;
    response.imbue(std::locale::classic());
    response << "HTTP/1.1 " << statusCode << ' ' << StatusText(statusCode) << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Cache-Control: no-store\r\n";
    if (statusCode == 405)
        response << "Allow: GET\r\n";
    if (g_allowWildcardCors.load(std::memory_order_acquire))
        response << "Access-Control-Allow-Origin: *\r\n";
    response << "\r\n" << body;
    SendAll(clientSocket, response.str());
}

void HandleClient(SOCKET clientSocket)
{
    DWORD timeoutMs = 1000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    std::string request;
    request.reserve(2048);
    char buffer[1024] = {};
    while (request.size() < kMaxRequestBytes) {
        const int received = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (received <= 0)
            break;
        request.append(buffer, static_cast<size_t>(received));
        if (request.find("\r\n\r\n") != std::string::npos)
            break;
    }

    int statusCode = 200;
    std::string body;
    const std::optional<RequestTarget> target = ParseRequestTarget(request);
    if (!target) {
        statusCode = 400;
        std::ostringstream out;
        AppendErrorBody(out, statusCode, "Malformed HTTP request");
        body = out.str();
    } else {
        body = BuildJsonForTarget(*target, statusCode);
    }

    SendHttpResponse(clientSocket, statusCode, body);
}

void ServerLoop(SOCKET listenSocket, std::shared_ptr<std::atomic<bool>> runFlag, uint16_t port)
{
    Diagnostics::Info("Unleashed test server listening on %s:%u.", kBindAddress, port);
    while (runFlag && runFlag->load(std::memory_order_acquire)) {
        sockaddr_in clientAddress{};
        int clientAddressLen = sizeof(clientAddress);
        const SOCKET clientSocket = accept(
            listenSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressLen);
        if (clientSocket == INVALID_SOCKET) {
            if (runFlag && runFlag->load(std::memory_order_acquire)) {
                Diagnostics::Warn("Unleashed test server accept failed: %d.", WSAGetLastError());
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            continue;
        }

        HandleClient(clientSocket);
        closesocket(clientSocket);
    }
    Diagnostics::Info("Unleashed test server stopped.");
}

} // namespace

bool IsCompiledIn()
{
    return true;
}

bool Start(const Options& options)
{
    std::lock_guard<std::mutex> lock(g_lifecycleMutex);
    if (g_running.load(std::memory_order_acquire)) {
        const uint16_t currentPort = g_port.load(std::memory_order_acquire);
        const bool currentCors = g_allowWildcardCors.load(std::memory_order_acquire);
        if (options.port != currentPort || options.allowWildcardCors != currentCors) {
            Diagnostics::Warn("Unleashed test server already running on %s:%u; refused restart with different options.",
                kBindAddress,
                currentPort);
            return false;
        }
        return true;
    }

    if (options.port == 0) {
        Diagnostics::Error("Unleashed test server port 0 is not supported.");
        return false;
    }

    WSADATA wsaData{};
    const int startupResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupResult != 0) {
        Diagnostics::Error("Unleashed test server WSAStartup failed: %d.", startupResult);
        return false;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        Diagnostics::Error("Unleashed test server socket failed: %d.", WSAGetLastError());
        WSACleanup();
        return false;
    }

    BOOL reuseAddress = TRUE;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    inet_pton(AF_INET, kBindAddress, &address.sin_addr);

    if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        Diagnostics::Error("Unleashed test server bind failed on %s:%u: %d.",
            kBindAddress,
            options.port,
            WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        Diagnostics::Error("Unleashed test server listen failed: %d.", WSAGetLastError());
        closesocket(listenSocket);
        WSACleanup();
        return false;
    }

    g_port.store(options.port, std::memory_order_release);
    g_allowWildcardCors.store(options.allowWildcardCors, std::memory_order_release);
    g_listenSocket = listenSocket;
    g_runFlag = std::make_shared<std::atomic<bool>>(true);
    {
        std::lock_guard<std::mutex> historyLock(g_targetHistoryMutex);
        g_targetHistory.clear();
        g_targetHistorySequence = 0;
    }
    g_running.store(true, std::memory_order_release);
    g_targetHistoryThread = std::thread(TargetHistoryLoop, g_runFlag);
    g_serverThread = std::thread(ServerLoop, listenSocket, g_runFlag, options.port);
    return true;
}

void Stop()
{
    std::thread threadToJoin;
    std::thread historyThreadToJoin;
    {
        std::lock_guard<std::mutex> lock(g_lifecycleMutex);
        if (!g_running.exchange(false, std::memory_order_acq_rel))
            return;

        if (g_runFlag)
            g_runFlag->store(false, std::memory_order_release);

        if (g_listenSocket != INVALID_SOCKET) {
            shutdown(g_listenSocket, SD_BOTH);
            closesocket(g_listenSocket);
            g_listenSocket = INVALID_SOCKET;
        }

        if (g_serverThread.joinable())
            threadToJoin = std::move(g_serverThread);
        if (g_targetHistoryThread.joinable())
            historyThreadToJoin = std::move(g_targetHistoryThread);
        g_runFlag.reset();
        g_allowWildcardCors.store(false, std::memory_order_release);
    }

    if (threadToJoin.joinable())
        threadToJoin.join();
    if (historyThreadToJoin.joinable())
        historyThreadToJoin.join();
    WSACleanup();
}

bool IsRunning()
{
    return g_running.load(std::memory_order_acquire);
}

} // namespace TestServer

#else

#include "Utils/Diagnostics.hpp"

namespace TestServer {

bool IsCompiledIn()
{
    return false;
}

bool Start(const Options&)
{
    Diagnostics::Error("Unleashed test server was not compiled in. Reconfigure with UNLEASHED_TEST_SERVER=ON.");
    return false;
}

void Stop()
{
}

bool IsRunning()
{
    return false;
}

} // namespace TestServer

#endif
