
#include "DebugService.h"
#include <App/Network/NetworkService.h>
#include <App/World/NetworkWorldSystem.h>
#include <App/World/ChatSystem.h>

namespace App
{
    DebugService::DebugService()
    {
    }

    DebugService::~DebugService()
    {
    }

    void DebugService::OnBootstrap()
    {
    }

    void DebugService::Draw()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Test"))
            {
                if (ImGui::Button("Connect"))
                {
                    Core::Container::Get<NetworkService>()->Connect("127.0.0.1:11778");

                    /*auto handle = Red::GetGameSystem<NetworkWorldSystem>();
                    Red::EntityID id;
                    Red::ScriptGameInstance game;
                    Red::CallVirtual(handle, "CreatePuppet", id, game);*/
                }
                // The "/" chat hotkey does not work on 2.31 - the mod ships a 2.2-era
                // prototype_hud.inkhud, and that is what registers the key listener. These
                // send chat commands straight through ChatSystem so debugging does not
                // depend on the broken UI.
                ImGui::Separator();

                if (ImGui::Button("Spawn dummy player"))
                {
                    SendChatCommand("/dummy");
                }

                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

    }

    void DebugService::SendChatCommand(const char* apCommand)
    {
        const auto pService = Core::Container::Get<NetworkService>();
        if (!pService || !pService->IsConnected())
        {
            spdlog::warn("[Debug] not connected - connect before sending '{}'", apCommand);
            return;
        }

        const auto world = Red::GetGameSystem<NetworkWorldSystem>();
        if (!world)
        {
            spdlog::error("[Debug] NetworkWorldSystem is not available");
            return;
        }

        const auto chat = world->GetChatSystem();
        if (!chat)
        {
            spdlog::error("[Debug] ChatSystem is not available");
            return;
        }

        spdlog::info("[Debug] sending chat command '{}'", apCommand);
        chat->Send(Red::CString(apCommand));
    }
}
