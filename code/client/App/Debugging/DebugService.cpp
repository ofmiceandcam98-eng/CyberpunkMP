
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
        // Nothing at all unless debug mode is on. A developer toolbar sitting over the
        // top of the game is confusing for anyone who is just here to play, and it is the
        // first thing a screenshot shows.
        if (!Settings::Get().debug)
            return;

        // The numbers the character selector goes by: screens are authored at 1920x1080, and one
        // authored unit is display height / 1080 real pixels (MpCsPixelsPerUnit in
        // CharacterSelect.reds). ImGui reads the display from the window every frame, so its
        // coordinates are real pixels and a place on screen can be worked out rather than hoped for.
        const auto& io = ImGui::GetIO();
        const auto perUnit = io.DisplaySize.y > 1.f ? io.DisplaySize.y / 1080.f : 1.f;

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("Test"))
            {
                DrawTestButtons();
                ImGui::EndMenu();
            }

            DrawConnectionStatus();

            ImGui::EndMainMenuBar();
        }

        DrawPanel(perUnit);

        if (m_screenCheck)
            DrawScreenCheck(perUnit);
    }

    void DebugService::DrawTestButtons()
    {
        if (ImGui::Button("Connect"))
        {
            // Dial whatever the launch arguments asked for, NOT a hardcoded
            // localhost. This button used to always connect to 127.0.0.1, so a
            // player who had set --ip= correctly still ended up talking to their
            // own machine and seeing an empty server.
            const auto address = fmt::format("{}:{}", Settings::Get().ip, Settings::Get().port);
            spdlog::info("[Debug] Connect pressed - dialling {}", address);

            const auto& service = Core::Container::Get<NetworkService>();

            // Same pairing as the real connect path: say where this connect is
            // coming from before dialling, or authentication has no basis on which
            // to decide whether it may spawn us. This button is pressed from inside
            // the world, so it captures a yes and behaves exactly as it always has.
            service->CaptureSpawnIntent();
            service->Connect(address);

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

        // On 2.5 you see yourself as the template body. These dress your own body from
        // your stored look the way other players are dressed; nothing is saved. Try
        // "new keys only" first - see NetworkWorldSystem::DressLocalBodyFromStoredLook.
        ImGui::Separator();

        if (ImGui::Button("Dress my body: new keys only"))
        {
            DressLocalBody(false);
        }

        if (ImGui::Button("Dress my body: replace template keys"))
        {
            DressLocalBody(true);
        }
    }

    void DebugService::DrawPanel(const float aPerUnit)
    {
        const auto& io = ImGui::GetIO();

        // Top centre at authored y 64: under the bar and clear of every screen's title. Placed
        // every frame rather than first use only, so where it is can be read off the numbers.
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 64.f * aPerUnit), ImGuiCond_Always, ImVec2(0.5f, 0.f));

        constexpr auto flags =
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Night City Online dev", nullptr, flags))
        {
            ImGui::Text("display %.0fx%.0f   %.3f px per unit", io.DisplaySize.x, io.DisplaySize.y, aPerUnit);
            ImGui::Text("mouse %.0f, %.0f px   %.0f, %.0f units", io.MousePos.x, io.MousePos.y,
                        io.MousePos.x / aPerUnit, io.MousePos.y / aPerUnit);
            ImGui::Checkbox("Screen check", &m_screenCheck);
            ImGui::Separator();

            DrawTestButtons();

            // An auto-sized window settles on its second frame, so the third is the one to report.
            static auto s_frames = 0;
            if (s_frames < 3 && ++s_frames == 3)
            {
                const auto pos = ImGui::GetWindowPos();
                const auto size = ImGui::GetWindowSize();
                const auto inside = pos.x >= 0.f && pos.y >= 0.f && pos.x + size.x <= io.DisplaySize.x &&
                                    pos.y + size.y <= io.DisplaySize.y;

                spdlog::info("[Debug] panel at {:.0f},{:.0f} size {:.0f}x{:.0f} on display {:.0f}x{:.0f}, {:.3f} px per unit - {}",
                             pos.x, pos.y, size.x, size.y, io.DisplaySize.x, io.DisplaySize.y, aPerUnit,
                             inside ? "on screen" : "OFF SCREEN");
            }
        }

        ImGui::End();
    }

    /*
     * The selector's click map, for the overlay: a frame on the very edge of the display, rulers
     * in authored units, and a crosshair on the mouse as ImGui has it. If the frame is not on
     * the screen's edges the overlay's space is not the screen's, and if the crosshair is not
     * under the cursor the buttons cannot be pressed. Either shows in one look.
     */
    void DebugService::DrawScreenCheck(const float aPerUnit) const
    {
        const auto& io = ImGui::GetIO();
        auto* pDraw = ImGui::GetForegroundDrawList();

        // --glitch in docs/DESIGN-LANGUAGE.md: the click, and nothing else.
        const auto cyan = IM_COL32(60, 232, 239, 255);
        const auto plate = IM_COL32(4, 4, 8, 220);
        const auto width = io.DisplaySize.x;
        const auto height = io.DisplaySize.y;
        const auto thick = 2.f * aPerUnit;

        const auto note = [&](float aX, float aY, const char* acpText)
        {
            const auto size = ImGui::CalcTextSize(acpText);
            const auto x = std::clamp(aX, 0.f, std::max(0.f, width - size.x - 8.f));
            const auto y = std::clamp(aY, 0.f, std::max(0.f, height - size.y - 4.f));

            pDraw->AddRectFilled(ImVec2(x, y), ImVec2(x + size.x + 8.f, y + size.y + 4.f), plate);
            pDraw->AddText(ImVec2(x + 4.f, y + 2.f), cyan, acpText);
        };

        pDraw->AddRect(ImVec2(thick / 2.f, thick / 2.f), ImVec2(width - thick / 2.f, height - thick / 2.f), cyan, 0.f, 0, thick);

        char text[64];
        const auto fontSize = ImGui::GetFontSize();

        for (auto unit = 100; static_cast<float>(unit) * aPerUnit < width; unit += 100)
        {
            const auto x = static_cast<float>(unit) * aPerUnit;
            const auto major = unit % 200 == 0;

            pDraw->AddLine(ImVec2(x, height), ImVec2(x, height - (major ? 16.f : 9.f) * aPerUnit), cyan, thick);

            if (major)
            {
                snprintf(text, sizeof(text), "%d", unit);
                note(x - fontSize, height - 18.f * aPerUnit - fontSize - 4.f, text);
            }
        }

        for (auto unit = 100; static_cast<float>(unit) * aPerUnit < height; unit += 100)
        {
            const auto y = static_cast<float>(unit) * aPerUnit;
            const auto major = unit % 200 == 0;

            pDraw->AddLine(ImVec2(width, y), ImVec2(width - (major ? 16.f : 9.f) * aPerUnit, y), cyan, thick);

            if (major)
            {
                snprintf(text, sizeof(text), "%d", unit);
                note(width - 18.f * aPerUnit - 2.5f * fontSize, y - fontSize / 2.f, text);
            }
        }

        if (!ImGui::IsMousePosValid())
            return;

        // Open in the middle, so whatever is under the point stays visible.
        const auto mx = io.MousePos.x;
        const auto my = io.MousePos.y;
        const auto gap = 8.f * aPerUnit;
        const auto arm = 32.f * aPerUnit;
        const auto line = 3.f * aPerUnit;

        pDraw->AddLine(ImVec2(mx - gap - arm, my), ImVec2(mx - gap, my), cyan, line);
        pDraw->AddLine(ImVec2(mx + gap, my), ImVec2(mx + gap + arm, my), cyan, line);
        pDraw->AddLine(ImVec2(mx, my - gap - arm), ImVec2(mx, my - gap), cyan, line);
        pDraw->AddLine(ImVec2(mx, my + gap), ImVec2(mx, my + gap + arm), cyan, line);

        snprintf(text, sizeof(text), "%.0f, %.0f", mx / aPerUnit, my / aPerUnit);

        // Flipped near the far edges: a clamped readout would slide over the point it describes.
        const auto size = ImGui::CalcTextSize(text);
        const auto tx = mx + gap + 8.f + size.x + 8.f > width ? mx - gap - 8.f - size.x - 8.f : mx + gap + 8.f;
        const auto ty = my + gap + 8.f + size.y + 4.f > height ? my - gap - 8.f - size.y - 4.f : my + gap + 8.f;

        note(tx, ty, text);
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

    void DebugService::DressLocalBody(bool aReplaceTemplateKeys)
    {
        const auto world = Red::GetGameSystem<NetworkWorldSystem>();
        if (!world)
        {
            spdlog::error("[Debug] NetworkWorldSystem is not available");
            return;
        }

        spdlog::info("[Debug] dress my body pressed ({})", aReplaceTemplateKeys ? "replace template keys" : "new keys only");
        world->DressLocalBodyFromStoredLook(aReplaceTemplateKeys);
    }
}
