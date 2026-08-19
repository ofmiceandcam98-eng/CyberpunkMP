#include "Spawning.h"
#include "Idling.h"
#include "Jogging.h"
#include "Sprinting.h"
#include "Walking.h"

namespace States
{
void Spawning::Enter() noexcept
{
}

std::optional<Base::Transition> Spawning::Process(const Update& acEvent) noexcept
{
    if (acEvent.Speed >= kSprintSpeed)
        return Transit<Sprinting>();
    if (acEvent.Speed >= kJogSpeed)
        return Transit<Jogging>();
    if (acEvent.Speed >= kWalkSpeed)
        return Transit<Walking>();

    return Transit<Idling>();
}
} // namespace States
