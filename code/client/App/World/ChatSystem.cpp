#include "ChatSystem.h"

#include "App/Network/NetworkService.h"
#include "App/ChatMessageEvent.h"
#include "App/Settings.h"
#include <RED4ext/Scripting/Natives/Generated/game/ui/IGameSystemUI.hpp>

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
