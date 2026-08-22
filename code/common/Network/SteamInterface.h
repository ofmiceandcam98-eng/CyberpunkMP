#pragma once

#include "steam/steamnetworkingsockets.h"


enum EPacketFlags
{
    kReliable,
    kUnreliable
};

enum EConnectOpcode : uint8_t
{
    kPayload = 0,
    kServerTime = 1,
    kCompressedPayload = 2,
    kHandshake = 3,

    // A one-byte refusal reason sent just before a handshake-level kick. Without it a
    // protocol-identifier mismatch closes the connection before the application layer
    // ever runs, and the player sees a bare disconnect while the real reason sits in a
    // server log they cannot read. Clients older than this opcode ignore it silently
    // (release builds no-op unknown opcodes), so the readable denial only pays off from
    // the next protocol divergence after both sides carry it - which is fine: it is for
    // every future mismatch, not the one that ships it.
    kRefused = 4
};

struct SteamInterface
{
    static void Acquire();
    static void Release();
};

using ConnectionId = HSteamNetConnection;

