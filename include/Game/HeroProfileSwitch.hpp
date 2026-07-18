#pragma once

#include <cstdint>
#include <optional>

namespace OW {

struct HeroProfileSwitch {
    uint64_t previousHeroId = 0;
    uint64_t previousLinkBase = 0;
    uint64_t nextHeroId = 0;
    uint64_t nextLinkBase = 0;
};

class HeroProfileSwitchDebouncer {
public:
    explicit HeroProfileSwitchDebouncer(uint64_t stableWindowMs = 300) noexcept
        : stableWindowMs_(stableWindowMs)
    {
    }

    std::optional<HeroProfileSwitch> Observe(uint64_t heroId,
                                             uint64_t linkBase,
                                             uint64_t nowMs,
                                             bool validHero) noexcept
    {
        if (!validHero || heroId == 0) {
            ClearCandidate();
            return std::nullopt;
        }

        if (heroId == confirmedHeroId_) {
            ClearCandidate();
            return std::nullopt;
        }

        if (candidateHeroId_ != heroId) {
            candidateHeroId_ = heroId;
            candidateLinkBase_ = linkBase;
            candidateSinceMs_ = nowMs;
            return std::nullopt;
        }

        if (linkBase != 0)
            candidateLinkBase_ = linkBase;
        if (nowMs - candidateSinceMs_ < stableWindowMs_)
            return std::nullopt;

        HeroProfileSwitch result{};
        result.previousHeroId = confirmedHeroId_;
        result.previousLinkBase = confirmedLinkBase_;
        result.nextHeroId = candidateHeroId_;
        result.nextLinkBase = candidateLinkBase_;

        confirmedHeroId_ = candidateHeroId_;
        confirmedLinkBase_ = candidateLinkBase_;
        ClearCandidate();
        return result;
    }

    void Reset() noexcept
    {
        confirmedHeroId_ = 0;
        confirmedLinkBase_ = 0;
        ClearCandidate();
    }

    uint64_t ConfirmedHeroId() const noexcept { return confirmedHeroId_; }
    uint64_t ConfirmedLinkBase() const noexcept { return confirmedLinkBase_; }

private:
    void ClearCandidate() noexcept
    {
        candidateHeroId_ = 0;
        candidateLinkBase_ = 0;
        candidateSinceMs_ = 0;
    }

    uint64_t stableWindowMs_ = 300;
    uint64_t confirmedHeroId_ = 0;
    uint64_t confirmedLinkBase_ = 0;
    uint64_t candidateHeroId_ = 0;
    uint64_t candidateLinkBase_ = 0;
    uint64_t candidateSinceMs_ = 0;
};

} // namespace OW
