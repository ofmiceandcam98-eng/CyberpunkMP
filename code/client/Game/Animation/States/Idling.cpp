#include "Idling.h"

#include "Jogging.h"
#include "Sprinting.h"
#include "Walking.h"

namespace States
{
void Idling::Enter() noexcept
{
}

std::optional<Base::Transition> Idling::Process(const Update& acEvent) noexcept
{
    if (acEvent.Speed >= kSprintSpeed)
        return Transit<Sprinting>();
    if (acEvent.Speed >= kJogSpeed)
        return Transit<Jogging>();
    if (acEvent.Speed >= kWalkSpeed)
        return Transit<Walking>();

    return std::nullopt;
}
} // namespace States
