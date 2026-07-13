#include "Game/OutputOwnership.hpp"

#include <tuple>

namespace OW
{
    bool OutputControl::operator<(const OutputControl& other) const noexcept
    {
        return std::tie(kind, code) < std::tie(other.kind, other.code);
    }

    bool OwnerToken::operator<(const OwnerToken& other) const noexcept
    {
        return std::tie(source, id, backendGeneration) <
            std::tie(other.source, other.id, other.backendGeneration);
    }

    OutputOwnership::OutputOwnership(std::uint64_t backendGeneration) noexcept
        : backendGeneration_(backendGeneration)
    {
    }

    std::uint64_t OutputOwnership::BackendGeneration() const noexcept
    {
        return backendGeneration_;
    }

    bool OutputOwnership::TrySetBackendGeneration(
        std::uint64_t backendGeneration) noexcept
    {
        if (!owners_.empty())
            return false;

        backendGeneration_ = backendGeneration;
        return true;
    }

    std::optional<OutputTransition> OutputOwnership::Acquire(
        const OutputControl& control,
        const OwnerToken& owner)
    {
        if (!IsValidControl(control) || !IsValidOwner(owner))
            return std::nullopt;

        auto& owners = owners_[control];
        const bool wasEmpty = owners.empty();
        const auto [unused, inserted] = owners.insert(owner);
        (void)unused;

        if (!inserted)
            return OutputTransition::None;
        return wasEmpty ? OutputTransition::Press : OutputTransition::None;
    }

    std::optional<OutputTransition> OutputOwnership::Release(
        const OutputControl& control,
        const OwnerToken& owner)
    {
        if (!IsValidControl(control) || !IsValidOwner(owner))
            return std::nullopt;

        const auto controlIt = owners_.find(control);
        if (controlIt == owners_.end())
            return OutputTransition::None;

        auto& owners = controlIt->second;
        if (owners.erase(owner) == 0)
            return OutputTransition::None;
        if (!owners.empty())
            return OutputTransition::None;

        owners_.erase(controlIt);
        return OutputTransition::Release;
    }

    std::optional<std::vector<OutputChange>> OutputOwnership::CancelOwner(
        const OwnerToken& owner)
    {
        if (!IsValidOwner(owner))
            return std::nullopt;

        std::vector<OutputChange> changes;
        for (auto controlIt = owners_.begin(); controlIt != owners_.end();) {
            auto& owners = controlIt->second;
            if (owners.erase(owner) == 0 || !owners.empty()) {
                ++controlIt;
                continue;
            }

            changes.push_back({ controlIt->first, OutputTransition::Release });
            controlIt = owners_.erase(controlIt);
        }
        return changes;
    }

    std::vector<OutputChange> OutputOwnership::CancelAll()
    {
        std::vector<OutputChange> changes;
        changes.reserve(owners_.size());
        for (const auto& [control, owners] : owners_) {
            if (!owners.empty())
                changes.push_back({ control, OutputTransition::Release });
        }
        owners_.clear();
        return changes;
    }

    bool OutputOwnership::IsHeld(const OutputControl& control) const
    {
        const auto it = owners_.find(control);
        return it != owners_.end() && !it->second.empty();
    }

    std::size_t OutputOwnership::OwnerCount(const OutputControl& control) const
    {
        const auto it = owners_.find(control);
        return it == owners_.end() ? 0 : it->second.size();
    }

    std::size_t OutputOwnership::HeldControlCount() const noexcept
    {
        return owners_.size();
    }

    bool OutputOwnership::Empty() const noexcept
    {
        return owners_.empty();
    }

    std::vector<OutputControl> OutputOwnership::HeldControls() const
    {
        std::vector<OutputControl> controls;
        controls.reserve(owners_.size());
        for (const auto& [control, owners] : owners_) {
            if (!owners.empty())
                controls.push_back(control);
        }
        return controls;
    }

    bool OutputOwnership::IsValidControl(
        const OutputControl& control) const noexcept
    {
        switch (control.kind) {
        case OutputControlKind::MouseButton:
            return control.code <= 2;
        case OutputControlKind::KeyboardUsage:
            return control.code >= 0x04 && control.code <= 0xDF;
        case OutputControlKind::KeyboardModifier:
            return control.code >= 0xE0 && control.code <= 0xE7;
        default:
            return false;
        }
    }

    bool OutputOwnership::IsValidOwner(const OwnerToken& owner) const noexcept
    {
        if (owner.id == 0 || owner.backendGeneration != backendGeneration_)
            return false;

        switch (owner.source) {
        case OutputOwnerSource::HeroTimedAction:
        case OutputOwnerSource::ZaryaPulse:
        case OutputOwnerSource::GlobalAim:
        case OutputOwnerSource::Trigger:
        case OutputOwnerSource::Sequence:
        case OutputOwnerSource::Viewpoint:
        case OutputOwnerSource::GenjiCombo:
        case OutputOwnerSource::GameAction:
        case OutputOwnerSource::Test:
            return true;
        default:
            return false;
        }
    }
}
