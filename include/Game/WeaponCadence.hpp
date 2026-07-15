#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace OW {

struct WeaponCadenceEntry {
    std::string weaponId;
    std::optional<float> triggerCycleIntervalMs;
    std::string firePattern;
    std::string integrationStatus;
};

// Experimental runtime data layer. It deliberately joins only cadence onto an
// existing WeaponSpec; it never creates or mutates weapon/action definitions.
class WeaponCadenceTable {
public:
    bool LoadTsv(const std::filesystem::path& path, std::string* error = nullptr);

    const WeaponCadenceEntry* Find(std::string_view weaponId) const;
    bool Loaded() const noexcept;
    std::size_t SourceRowCount() const noexcept;
    std::size_t EligibleRowCount() const noexcept;
    std::size_t DiscreteIntervalCount() const noexcept;
    std::size_t Size() const noexcept;
    const std::filesystem::path& LoadedPath() const noexcept;
    const std::string& LastError() const noexcept;

private:
    std::map<std::string, WeaponCadenceEntry, std::less<>> entries_;
    std::filesystem::path loadedPath_;
    std::string lastError_;
    std::size_t sourceRowCount_ = 0;
    std::size_t eligibleRowCount_ = 0;
    std::size_t discreteIntervalCount_ = 0;
};

const WeaponCadenceTable& RuntimeWeaponCadenceTable();
const WeaponCadenceEntry* FindRuntimeWeaponCadence(std::string_view weaponId);
std::optional<float> ResolveWeaponTriggerCycleIntervalMs(std::string_view weaponId);

} // namespace OW
