#pragma once

#include "Core/Foundation/Feature.hpp"
#include "Core/Logging/LoggingAgent.hpp"

namespace App
{
struct DebugService : Core::Feature, Core::LoggingAgent
{
    DebugService();
    ~DebugService() override;

    void OnBootstrap() override;

    void Draw();

private:
    // Sends a chat command without going through the in-game chat UI, whose "/" hotkey
    // does not work on 2.31.
    static void SendChatCommand(const char* apCommand);
};
} // namespace App
