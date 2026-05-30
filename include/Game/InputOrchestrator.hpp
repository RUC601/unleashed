#pragma once

#include "Game/AimArchitecture.hpp"

namespace OW {

struct ExecutionToken {
    ExecutionSource source = ExecutionSource::GlobalAim;
    int priority = 0;
    bool active = false;
};

inline int ExecutionPriority(ExecutionSource source)
{
    switch (source) {
    case ExecutionSource::SequenceInternal:
        return 100;
    case ExecutionSource::HeroSkill:
        return 60;
    case ExecutionSource::Trigger:
        return 40;
    case ExecutionSource::GlobalAim:
    default:
        return 20;
    }
}

inline ExecutionToken MakeExecutionToken(ExecutionSource source, bool active = true)
{
    return ExecutionToken{ source, ExecutionPriority(source), active };
}

inline bool ShouldYieldToExecution(const ExecutionToken& current, const ExecutionToken& owner)
{
    return owner.active && owner.priority > current.priority;
}

} // namespace OW
