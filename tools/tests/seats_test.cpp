// Vehicle seats: ids are derived from names, and an invented id is not a seat.
//
// Tests the SHIPPED VehicleSeats.h. The fifth-person bug was that occupancy was checked and
// seat IDENTITY never was, so any 64-bit number looked like an empty seat.

#include <cstdio>
#include "VehicleSeats.h"

static int failures = 0;
static void Check(bool c, const char* what)
{
    std::printf("%s  %s\n", c ? "ok  " : "FAIL", what);
    if (!c) ++failures;
}

int main()
{
    // The one id the game gave us, from the constant Level.cpp always carried.
    static_assert(Fnv1a64("seat_front_left") == 0xb000b1d029d0cea0ULL,
                  "seat_front_left must reproduce the id the client actually sends");

    Check(kDriverSeat == 0xb000b1d029d0cea0ULL, "kDriverSeat is the known front-left id");

    // The three NextOccupant used to carry as pasted literals. If these ever diverge, a
    // disconnecting driver silently hands the car to the wrong passenger.
    Check(Fnv1a64("seat_front_right") == 0x63c846db887c0035ULL, "seat_front_right matches the old literal");
    Check(Fnv1a64("seat_back_left")   == 0xb06da35221954b3eULL, "seat_back_left matches the old literal");
    Check(Fnv1a64("seat_back_right")  == 0xc90fa7831f484433ULL, "seat_back_right matches the old literal");

    Check(IsKnownSeat(kDriverSeat), "the driver seat is a known seat");
    Check(IsKnownSeat(Fnv1a64("seat_back_right")), "a back seat is a known seat");

    // The fifth-player fix.
    Check(!IsKnownSeat(0), "zero is not a seat");
    Check(!IsKnownSeat(0xdeadbeefdeadbeefULL), "an invented id is not a seat");
    Check(!IsKnownSeat(kDriverSeat + 1), "a near-miss of a real id is not a seat");

    bool distinct = true;
    for (size_t i = 0; i < kVehicleSeatCount; ++i)
        for (size_t j = i + 1; j < kVehicleSeatCount; ++j)
            if (kVehicleSeats[i].Id == kVehicleSeats[j].Id) distinct = false;
    Check(distinct, "all seat ids are distinct");

    Check(VehicleSeatName(kDriverSeat) == "seat_front_left", "names resolve for logging");
    Check(VehicleSeatName(1234).rfind("unknown(", 0) == 0, "an unknown id still names itself");
    Check(VehicleSeatDescription(Fnv1a64("seat_back_left")) == "the back left seat",
          "descriptions read like something you would say to a player");

    std::printf("\n%zu seats known. %s\n", kVehicleSeatCount, failures ? "FAILURES" : "all passed");
    return failures ? 1 : 0;
}
