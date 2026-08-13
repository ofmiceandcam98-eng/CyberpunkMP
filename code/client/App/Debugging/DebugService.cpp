
#include "DebugService.h"
#include <App/Network/NetworkService.h>
#include <App/Settings.h>
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
                    // Dial whatever the launch arguments asked for, NOT a hardcoded
                    // localhost. This button used to always connect to 127.0.0.1, so a
                    // player who had set --ip= correctly still ended up talking to their
                    // own machine and seeing an empty server.
                    const auto address = fmt::format("{}:{}", Settings::Get().ip, Settings::Get().port);
                    spdlog::info("[Debug] Connect pressed - dialling {}", address);
                    Core::Container::Get<NetworkService>()->Connect(address);

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

            DrawConnectionStatus();

            ImGui::EndMainMenuBar();
        }

    }

    void DebugService::DrawConnectionStatus()
    {
        const auto pService = Core::Container::Get<NetworkService>();

        if (!pService || !pService->IsConnected())
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 110.f);
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.f), "offline");
            return;
        }

        const auto status = pService->GetConnectionStatus();

        // GameNetworkingSockets reports -1 for "not measured yet", which is normal for
        // the first seconds of a connection. Treating that as a quality value produced
        // "200% loss - UNSTABLE" on a perfectly healthy link, which is worse than
        // showing nothing: it tells people their connection is broken when it is not.
        const bool localKnown = status.m_flConnectionQualityLocal >= 0.f;
        const bool remoteKnown = status.m_flConnectionQualityRemote >= 0.f;

        if (!localKnown && !remoteKnown)
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 130.f);
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.f), "%dms  measuring...", status.m_nPing);
            return;
        }

        // Worst of the two known directions - a link is only as good as its weaker end.
        float quality = 1.f;
        if (localKnown) quality = std::min(quality, status.m_flConnectionQualityLocal);
        if (remoteKnown) quality = std::min(quality, status.m_flConnectionQualityRemote);

        // Green / amber / red, so the state reads at a glance without parsing numbers.
        ImVec4 colour{0.42f, 0.82f, 0.55f, 1.f};
        const char* label = "";

        if (status.m_nPing > 250 || quality < 0.90f)
        {
            colour = ImVec4(0.94f, 0.44f, 0.35f, 1.f);
            label = "  UNSTABLE";
        }
        else if (status.m_nPing > 120 || quality < 0.98f)
        {
            colour = ImVec4(0.95f, 0.72f, 0.30f, 1.f);
            label = "  poor";
        }

        char buffer[64];
        // Packet loss is more intuitive than "quality" for anyone reading it.
        snprintf(buffer, sizeof(buffer), "%dms  %.0f%% loss%s", status.m_nPing, (1.f - quality) * 100.f, label);

        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(buffer).x - 16.f);
        ImGui::TextColored(colour, "%s", buffer);
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
