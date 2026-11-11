#include "ChatSystem.h"

#include "App/Network/NetworkService.h"
#include "App/ChatMessageEvent.h"
#include "App/Settings.h"
#include <RED4ext/Scripting/Natives/Generated/game/ui/IGameSystemUI.hpp>
#include "Core/Facades/Runtime.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <system_error>

/**
 * Where the trade panel's placement persists: INSIDE the mod's own folder.
 *
 * It used to be %USERPROFILE%\nco-trade-placement.txt - a write location neither layer of the
 * uninstall footprint knew about, so an uninstall left it behind, which the footprint rule
 * forbids (Atlas: trade-placement-file-is-written-outside-the-uninstall-footprint). The mod's
 * folder is what uninstallMod removes whole, so a file here leaves with the mod.
 *
 * A new name as well as a new place: the old file holds positions relative to the CHAT BOX,
 * and the panel now places itself as a fraction of the SCREEN. Reading the old numbers as the
 * new kind would put the panel somewhere nobody chose.
 */
static std::filesystem::path TradePlacementPath()
{
    return Core::Runtime::GetModuleDir() / "trade-placement.txt";
}

// The old profile file, removed the first time placement is read or written, so machines that
// already have one end up with a clean footprint instead of a permanent leftover.
static void RemoveLegacyTradePlacement()
{
    if (const auto home = std::getenv("USERPROFILE"))
    {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(home) / "nco-trade-placement.txt", ec);
    }
}

void ChatSystem::OnWorldAttached(RED4ext::world::RuntimeScene* aScene)
{
    m_ready = true;
}

void ChatSystem::OnAfterWorldDetach()
{
    m_ready = false;
}

void ChatSystem::HandleChatMessage(const PacketEvent<server::ChatMessage>& aMessage)
{
    m_messages.push_back(aMessage);

    auto evt = reinterpret_cast<ChatMessageUIEvent*>(Red::GetClass("ChatMessageUIEvent")->CreateInstance());
    evt->author = RED4ext::CString(aMessage.get_username());
    evt->message = RED4ext::CString(aMessage.get_message());
    evt->channel = aMessage.get_channel();

    auto uiSystem = Red::GetGameSystem<RED4ext::game::ui::IGameSystemUI>();
    uiSystem->QueueEvent(RED4ext::Handle(evt));
}

void ChatSystem::OnInitialize(const RED4ext::JobHandle& aJob)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&ChatSystem::HandleChatMessage>(this);
}

void ChatSystem::Send(const Red::CString& aMessage)
{
    const auto pNetworkService = Core::Container::Get<NetworkService>();

    client::ChatMessageRequest request;
    request.set_message(aMessage.c_str());

    pNetworkService->Send(request);
}

RED4ext::CString ChatSystem::GetUsername()
{
    // Whoever the launcher signed in as. This is only what the local UI shows next to
    // your own messages - everyone else sees the name the SERVER has for you, which is
    // the one Discord vouched for once verification is enabled.
    const auto& name = Settings::Get().discordName;
    return RED4ext::CString(name.empty() ? "Player" : name.c_str());
}

void ChatSystem::SaveTradePlacement(float aX, float aY, float aZoom, float aGap)
{
    RemoveLegacyTradePlacement();

    try
    {
        std::ofstream file(TradePlacementPath(), std::ios::trunc);
        file << aX << ' ' << aY << ' ' << aZoom << ' ' << aGap << '\n';
    }
    catch (...)
    {
        // Non-load-bearing: if the file cannot be written the overlay just uses defaults.
    }
}

RED4ext::Vector4 ChatSystem::LoadTradePlacement()
{
    // (0,0,0,0) means "no saved placement" - the redscript side keeps its defaults then. A real
    // save always has zoom > 0, which is the sentinel the caller checks.
    RED4ext::Vector4 placement{0.f, 0.f, 0.f, 0.f};

    RemoveLegacyTradePlacement();

    try
    {
        std::ifstream file(TradePlacementPath());
        if (file)
            file >> placement.X >> placement.Y >> placement.Z >> placement.W;
    }
    catch (...)
    {
    }
    return placement;
}
