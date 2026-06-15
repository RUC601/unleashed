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

#include "Game/GameData.hpp"
#include "Game/HeroPerks.hpp"
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

std::string HeroName(uint64_t heroId)
{
    if (heroId == 0)
        return {};

    if (const char* name = OW::GameData::HeroName(heroId); name && name[0] != '\0')
        return name;

    if (OW::GameData::HasHeroIdPrefix(heroId)) {
        char fallback[24] = {};
        std::snprintf(fallback, sizeof(fallback), "Hero_%04llX",
            static_cast<unsigned long long>(heroId & 0xFFFFull));
        return fallback;
    }

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
        << "},\"local_entity\":{\"selected\":" << snapshot.localEntity.selected
        << ",\"selected_health\":" << snapshot.localEntity.selectedHealth
        << ",\"best_distance_cm\":" << snapshot.localEntity.bestDistanceCm
        << "}}}";
    return out.str();
}

void AppendHeroPerkJson(std::ostringstream& out, const OW::HeroPerks::State& perk)
{
    const OW::HeroPerks::Classification& classification = perk.classification;
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

std::string BuildLocalJson()
{
    std::vector<std::string> warnings;
    const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
    const OW::TargetingDetail::FovRuntimeContext fov = OW::TargetingDetail::SnapshotFovRuntimeContext();
    const OW::HeroPerks::State perk = OW::HeroPerks::ReadCurrent(local.HeroID, local.SkillBase);
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
    const std::string heroName = HeroName(local.HeroID);
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
        const std::string heroName = HeroName(entity.HeroID);
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
    if (target.path == "/api/local")
        return BuildLocalJson();
    if (target.path == "/api/entities")
        return BuildEntitiesJson(target.query);
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
