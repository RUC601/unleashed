#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace OW
{
    enum class OutputControlKind : std::uint8_t
    {
        MouseButton,
        KeyboardUsage,
        KeyboardModifier
    };

    struct OutputControl
    {
        OutputControlKind kind = OutputControlKind::MouseButton;
        std::uint16_t code = 0;

        bool operator==(const OutputControl&) const = default;
        bool operator<(const OutputControl& other) const noexcept;
    };

    enum class OutputOwnerSource : std::uint8_t
    {
        HeroTimedAction,
        ZaryaPulse,
        GlobalAim,
        Trigger,
        Sequence,
        Viewpoint,
        GenjiCombo,
        GameAction,
        Test
    };

    struct OwnerToken
    {
        OutputOwnerSource source = OutputOwnerSource::Test;
        std::uint64_t id = 0;
        std::uint64_t backendGeneration = 0;

        bool operator==(const OwnerToken&) const = default;
        bool operator<(const OwnerToken& other) const noexcept;
    };

    enum class OutputTransition : std::uint8_t
    {
        Press,
        Release,
        None
    };

    struct OutputChange
    {
        OutputControl control;
        OutputTransition transition = OutputTransition::None;

        bool operator==(const OutputChange&) const = default;
    };

    class OutputOwnership
    {
    public:
        explicit OutputOwnership(std::uint64_t backendGeneration = 0) noexcept;

        std::uint64_t BackendGeneration() const noexcept;

        // A generation may only change while no output is owned. Callers must
        // CancelAll before switching backend generations.
        bool TrySetBackendGeneration(std::uint64_t backendGeneration) noexcept;

        // nullopt means an invalid control/token or a stale backend generation.
        // OutputTransition::None means the valid operation was idempotent or did
        // not change the aggregate held state.
        std::optional<OutputTransition> Acquire(
            const OutputControl& control,
            const OwnerToken& owner);
        std::optional<OutputTransition> Release(
            const OutputControl& control,
            const OwnerToken& owner);

        // CancelOwner removes only the matching owner's leases. nullopt rejects
        // an invalid/stale token. Returned changes contain only aggregate ups.
        std::optional<std::vector<OutputChange>> CancelOwner(
            const OwnerToken& owner);
        std::vector<OutputChange> CancelAll();

        bool IsHeld(const OutputControl& control) const;
        std::size_t OwnerCount(const OutputControl& control) const;
        std::size_t HeldControlCount() const noexcept;
        bool Empty() const noexcept;
        std::vector<OutputControl> HeldControls() const;

    private:
        using OwnerSet = std::set<OwnerToken>;

        bool IsValidControl(const OutputControl& control) const noexcept;
        bool IsValidOwner(const OwnerToken& owner) const noexcept;

        std::uint64_t backendGeneration_ = 0;
        std::map<OutputControl, OwnerSet> owners_;
    };
}
