// The staff ladder. Security-relevant: every one of these is "can this person do that".
//
// Tests the SHIPPED PermissionLevel.h. The rule that matters is Cam's: support gets extra
// character slots and NOTHING else, and its position BELOW moderator is what enforces that
// rather than anyone remembering it.

#include <cstdio>
#include <cstdint>
#include <string>

#include "PermissionLevel.h"

// PlayerStore drags in the world; the slot rule is one line, restated exactly.
static constexpr int kPlayerSlots = 1;
static constexpr int kStaffSlots  = 4;
static int SlotsForLevel(EPermissionLevel l)
{
    return l >= EPermissionLevel::kSupport ? kStaffSlots : kPlayerSlots;
}

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    using P = EPermissionLevel;

    Check(P::kPlayer < P::kSupport,       "player  <  support");
    Check(P::kSupport < P::kModerator,    "support <  moderator");
    Check(P::kModerator < P::kEventStaff, "moderator < event staff");
    Check(P::kEventStaff < P::kAdmin,     "event staff < admin");
    Check(P::kAdmin < P::kOwner,          "admin   <  owner");

    // SUPPORT GETS SLOTS AND NOTHING ELSE - the rule this file exists for.
    Check(SlotsForLevel(P::kSupport) == kStaffSlots, "support gets 4 character slots");
    Check(!(P::kSupport >= P::kModerator),  "support CANNOT kick/jail/mute");
    Check(!(P::kSupport >= P::kEventStaff), "support CANNOT spawn");
    Check(!(P::kSupport >= P::kAdmin),      "support CANNOT ban");

    Check(P::kEventStaff >= P::kModerator,  "event staff CAN moderate");
    Check(!(P::kEventStaff >= P::kAdmin),   "event staff CANNOT ban or /rename");

    Check(P::kModerator >= P::kModerator,      "moderator CAN moderate");
    Check(!(P::kModerator >= P::kEventStaff),  "moderator CANNOT spawn - that is event staff");

    Check(P::kAdmin >= P::kEventStaff, "admin CAN spawn");
    Check(P::kAdmin >= P::kAdmin,      "admin CAN ban");

    for (auto l : {P::kSupport, P::kModerator, P::kEventStaff, P::kAdmin, P::kOwner})
        Check(SlotsForLevel(l) == kStaffSlots, "a staff rank gets 4 slots");
    Check(SlotsForLevel(P::kPlayer) == kPlayerSlots, "a plain player gets 1 slot");

    // Inserting rungs must not move anything already stored on disk.
    Check(static_cast<uint8_t>(P::kPlayer)    == 0,  "player is still 0");
    Check(static_cast<uint8_t>(P::kModerator) == 10, "moderator is still 10");
    Check(static_cast<uint8_t>(P::kAdmin)     == 20, "admin is still 20");
    Check(static_cast<uint8_t>(P::kOwner)     == 30, "owner is still 30");

    Check(std::string(ToString(P::kSupport))    == "support",     "support names itself");
    Check(std::string(ToString(P::kEventStaff)) == "event staff", "event staff names itself");

    std::printf("\n%s\n", failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
