#pragma once

struct AppearanceComponent
{
    // Raw TweakDBIDs. The server never interprets these - it stores what one client sent
    // and hands it to the others - so a number is strictly better than a name here.
    Vector<uint64_t> equipment;
    Vector<uint8_t> ccstate;
};