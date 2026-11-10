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

    // The 2.5 self-look experiment - see NetworkWorldSystem::DressLocalBodyFromStoredLook.
    static void DressLocalBody(bool aReplaceTemplateKeys);

    // Ping and packet loss, right-aligned in the menu bar, coloured by severity.
    static void DrawConnectionStatus();

    // The test actions, shared by the menu bar and the panel.
    static void DrawTestButtons();

    // aPerUnit is real pixels per authored unit, the number the character selector goes by.
    void DrawPanel(float aPerUnit);
    void DrawScreenCheck(float aPerUnit) const;

    bool m_screenCheck{true};
};
} // namespace App
