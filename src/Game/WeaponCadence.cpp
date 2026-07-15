#include "Game/WeaponCadence.hpp"

#include <Windows.h>

#include <charconv>
#include <cmath>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

namespace OW {
namespace {

constexpr wchar_t kCadenceFileName[] = L"overwatch_weapon_cadence_20260714.tsv";

bool IsRuntimeIntegrationStatus(std::string_view value)
{
    return value == "existing_weapon_spec" || value == "existing_code_variant";
}

std::vector<std::string> SplitTsvRow(std::string_view line)
{
    std::vector<std::string> fields;
    std::string field;
    field.reserve(line.size());
    bool quoted = false;

    for (std::size_t index = 0; index < line.size(); ++index) {
        const char value = line[index];
        if (value == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (value == '\t' && !quoted) {
            fields.push_back(std::move(field));
            field.clear();
        } else if (value != '\r') {
            field.push_back(value);
        }
    }
    fields.push_back(std::move(field));
    return fields;
}

std::optional<std::size_t> HeaderIndex(const std::vector<std::string>& header,
                                       std::string_view name)
{
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (header[index] == name)
            return index;
    }
    return std::nullopt;
}

std::string_view FieldAt(const std::vector<std::string>& fields, std::size_t index)
{
    return index < fields.size() ? std::string_view(fields[index]) : std::string_view{};
}

bool ParsePositiveFloat(std::string_view text, float& value)
{
    const char* first = text.data();
    const char* last = first + text.size();
    const auto parsed = std::from_chars(first, last, value);
    return parsed.ec == std::errc{} && parsed.ptr == last &&
        std::isfinite(value) && value > 0.0f;
}

std::filesystem::path ExecutableDirectory()
{
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size() - 1)
            return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();
        buffer.resize(buffer.size() * 2);
    }
}

std::optional<std::filesystem::path> EnvironmentCadencePath()
{
    constexpr wchar_t kVariable[] = L"UNLEASHED_WEAPON_CADENCE_TSV";
    const DWORD required = GetEnvironmentVariableW(kVariable, nullptr, 0);
    if (required == 0)
        return std::nullopt;

    std::vector<wchar_t> buffer(required);
    const DWORD written = GetEnvironmentVariableW(
        kVariable,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size())
        return std::nullopt;
    return std::filesystem::path(buffer.data(), buffer.data() + written);
}

std::vector<std::filesystem::path> RuntimeCandidatePaths()
{
    std::vector<std::filesystem::path> candidates;
    const auto append = [&](const std::filesystem::path& path) {
        if (path.empty())
            return;
        for (const auto& existing : candidates) {
            if (existing == path)
                return;
        }
        candidates.push_back(path);
    };

    if (const auto environment = EnvironmentCadencePath())
        append(*environment);

    const std::filesystem::path executableDirectory = ExecutableDirectory();
    if (!executableDirectory.empty())
        append(executableDirectory / L"data" / kCadenceFileName);

    std::error_code currentPathError;
    const std::filesystem::path currentPath = std::filesystem::current_path(currentPathError);
    if (!currentPathError)
        append(currentPath / L"data" / kCadenceFileName);

#ifdef WEAPON_CADENCE_SOURCE_PATH
    append(std::filesystem::path(WEAPON_CADENCE_SOURCE_PATH));
#endif

    return candidates;
}

} // namespace

bool WeaponCadenceTable::LoadTsv(const std::filesystem::path& path, std::string* error)
{
    entries_.clear();
    loadedPath_.clear();
    lastError_.clear();
    sourceRowCount_ = 0;
    eligibleRowCount_ = 0;
    discreteIntervalCount_ = 0;

    const auto fail = [&](std::string message) {
        entries_.clear();
        loadedPath_.clear();
        lastError_ = std::move(message);
        if (error)
            *error = lastError_;
        return false;
    };

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return fail("unable to open cadence TSV: " + path.string());

    std::string line;
    if (!std::getline(stream, line))
        return fail("cadence TSV has no header: " + path.string());

    std::vector<std::string> header = SplitTsvRow(line);
    if (!header.empty() && header.front().size() >= 3 &&
        static_cast<unsigned char>(header.front()[0]) == 0xEF &&
        static_cast<unsigned char>(header.front()[1]) == 0xBB &&
        static_cast<unsigned char>(header.front()[2]) == 0xBF) {
        header.front().erase(0, 3);
    }

    const auto weaponIdIndex = HeaderIndex(header, "weapon_id");
    const auto intervalIndex = HeaderIndex(header, "trigger_cycle_interval_ms");
    const auto patternIndex = HeaderIndex(header, "fire_pattern");
    const auto statusIndex = HeaderIndex(header, "integration_status");
    if (!weaponIdIndex || !intervalIndex || !patternIndex || !statusIndex)
        return fail("cadence TSV is missing a required runtime column: " + path.string());

    std::size_t lineNumber = 1;
    while (std::getline(stream, line)) {
        ++lineNumber;
        if (line.empty() || line == "\r")
            continue;

        ++sourceRowCount_;
        const std::vector<std::string> fields = SplitTsvRow(line);
        const std::string_view status = FieldAt(fields, *statusIndex);
        if (!IsRuntimeIntegrationStatus(status))
            continue;

        const std::string_view weaponId = FieldAt(fields, *weaponIdIndex);
        if (weaponId.empty())
            return fail("eligible cadence row has no weapon_id at line " + std::to_string(lineNumber));

        WeaponCadenceEntry entry{};
        entry.weaponId.assign(weaponId);
        entry.firePattern.assign(FieldAt(fields, *patternIndex));
        entry.integrationStatus.assign(status);

        const std::string_view interval = FieldAt(fields, *intervalIndex);
        if (!interval.empty()) {
            float parsedInterval = 0.0f;
            if (!ParsePositiveFloat(interval, parsedInterval)) {
                return fail("invalid trigger_cycle_interval_ms at line " +
                    std::to_string(lineNumber));
            }
            entry.triggerCycleIntervalMs = parsedInterval;
            ++discreteIntervalCount_;
        }

        const auto [position, inserted] = entries_.emplace(entry.weaponId, std::move(entry));
        if (!inserted)
            return fail("duplicate eligible cadence weapon_id: " + position->first);
        ++eligibleRowCount_;
    }

    if (entries_.empty())
        return fail("cadence TSV contains no eligible runtime rows: " + path.string());

    loadedPath_ = path;
    lastError_.clear();
    if (error)
        error->clear();
    return true;
}

const WeaponCadenceEntry* WeaponCadenceTable::Find(std::string_view weaponId) const
{
    const auto found = entries_.find(weaponId);
    return found == entries_.end() ? nullptr : &found->second;
}

bool WeaponCadenceTable::Loaded() const noexcept
{
    return !loadedPath_.empty() && !entries_.empty();
}

std::size_t WeaponCadenceTable::SourceRowCount() const noexcept
{
    return sourceRowCount_;
}

std::size_t WeaponCadenceTable::EligibleRowCount() const noexcept
{
    return eligibleRowCount_;
}

std::size_t WeaponCadenceTable::DiscreteIntervalCount() const noexcept
{
    return discreteIntervalCount_;
}

std::size_t WeaponCadenceTable::Size() const noexcept
{
    return entries_.size();
}

const std::filesystem::path& WeaponCadenceTable::LoadedPath() const noexcept
{
    return loadedPath_;
}

const std::string& WeaponCadenceTable::LastError() const noexcept
{
    return lastError_;
}

const WeaponCadenceTable& RuntimeWeaponCadenceTable()
{
    static const WeaponCadenceTable table = [] {
        WeaponCadenceTable candidate;
        for (const std::filesystem::path& path : RuntimeCandidatePaths()) {
            if (candidate.LoadTsv(path))
                break;
        }
        return candidate;
    }();
    return table;
}

const WeaponCadenceEntry* FindRuntimeWeaponCadence(std::string_view weaponId)
{
    return RuntimeWeaponCadenceTable().Find(weaponId);
}

std::optional<float> ResolveWeaponTriggerCycleIntervalMs(std::string_view weaponId)
{
    const WeaponCadenceEntry* entry = FindRuntimeWeaponCadence(weaponId);
    return entry ? entry->triggerCycleIntervalMs : std::nullopt;
}

} // namespace OW
