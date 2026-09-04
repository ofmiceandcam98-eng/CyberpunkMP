#include "RpcService.h"

#include <algorithm>   // std::max, for sizing the client table by highest id

#include "RpcValidator.h"
#include "App/Network/NetworkService.h"
#include <RED4ext/Scripting/Natives/GameTime.hpp>

#include "RpcGenerator.h"
#include "RpcPack.h"
#include "App/Settings.h"


RpcService::RpcService(RED4ext::PluginHandle aPlugin, const RED4ext::Sdk* aSdk)
{
    RED4ext::GameState state{nullptr, nullptr, nullptr};
    state.OnExit = &PrepareRpc;

    aSdk->gameStates->Add(aPlugin, RED4ext::EGameStateType::Initialization, &state);
}

RpcService::~RpcService()
{
    
}

void RpcService::OnInitialize()
{
    RpcValidator::Attach();

    const auto pNetworkService = Core::Container::Get<NetworkService>();
    pNetworkService->RegisterHandler<&RpcService::HandleRpc>(this);
    pNetworkService->RegisterHandler<&RpcService::HandleRpcDefinitions>(this);
}

void RpcService::OnShutdown()
{
}

std::optional<uint32_t> RpcService::GetRpcId(uint64_t aKlass, uint64_t aFunction) const
{
    const auto itor = m_serverRpcs.find(RpcId{aKlass, aFunction});
    if (itor != std::end(m_serverRpcs))
        return itor->second;

    return std::nullopt;
}

bool RpcService::PrepareRpc(RED4ext::CGameApplication* aApp)
{
    RpcValidator::InternalValidate();
    RpcGenerator::GenerateHandlers();
    RpcGenerator::DumpCsharp();

    if (Settings::Get().RpcOnly)
    {
        ExitProcess(0);
    }

    return true;
}

void RpcService::HandleRpc(const PacketEvent<server::RpcCall>& aMessage)
{
    if (!Call(aMessage))
        spdlog::error("Rpc failed");
}

void RpcService::HandleRpcDefinitions(const PacketEvent<server::RpcDefinitions>& aMessage)
{
    /*
     * Sized by the HIGHEST ID, not by how many definitions arrived.
     *
     * These are different numbers the moment ids stop being dense from zero, and the table
     * is written by id: `m_clientRpcs[rpc.get_id()]`. Sizing by count and indexing by id
     * means any id >= count is an out-of-bounds WRITE - heap corruption, in a handler that
     * runs on whatever the server sends.
     *
     * It is safe today only by coincidence: the server hands out `id = m_clientRpcs.size()`
     * at registration and serialises every slot, so ids are dense and max+1 == count. That
     * is a property of the current registration code, not a guarantee of the wire, and the
     * one operation that could break it is ADDING registrations - which is exactly what the
     * phone work does. Found while checking zeldfep's null-deref note (map, Known bugs);
     * same root, worse consequence, so both are fixed together.
     */
    size_t highest = 0;
    for (auto& rpc : aMessage.get_client_definitions())
        highest = std::max<size_t>(highest, static_cast<size_t>(rpc.get_id()) + 1);

    m_clientRpcs.clear();
    m_clientRpcs.resize(highest);

    // New table, so old refusals mean nothing - an id that had no handler under the last
    // set of definitions may be a different function now, and deserves to be reported again.
    m_refusedIds.clear();

    for (auto& rpc : aMessage.get_client_definitions())
    {
        const RpcId id{rpc.get_klass(), rpc.get_function()};
        m_clientRpcs[rpc.get_id()] = {id, RpcGenerator::GetRpcHandler(id.Klass, id.Function)};
    }

    for (auto& rpc : aMessage.get_server_definitions())
    {
        m_serverRpcs[RpcId{rpc.get_klass(), rpc.get_function()}] = rpc.get_id();
    }
}

bool RpcService::Call(const server::RpcCall& aMessage) const
{
    auto id = aMessage.get_id();

    if (id >= m_clientRpcs.size())
    {
        spdlog::error("Failed to retrieve Rpc with id {:X}", id);
        return false;
    }

    const auto& rpc = m_clientRpcs[id];

    const auto* pContext = rpc.Handler;

    /*
     * The guard was `if (rpc.Id.Klass != 0 && !pContext)`, which protects the wrong case.
     *
     * A slot the definitions never wrote is default-constructed: Klass == 0 AND a null
     * handler. That makes the first half of the condition false, so the whole guard is
     * false, so it does NOT return - and the next line dereferences the null handler.
     * The one shape that needed catching was the only one let through. Reported by
     * zeldfep from a read of this file (map, Known bugs, INFERRED); confirmed here.
     *
     * pContext is dereferenced unconditionally below, so the honest test is simply whether
     * it exists. Nothing that worked before stops working: any call that reached line 99
     * without crashing had a non-null handler already.
     *
     * This is also the OLD CLIENT case, and it has to be survivable rather than fatal. Ids
     * are negotiated per connection, so a server that registers an RPC this build does not
     * implement will call an id whose handler is null here. Refusing with a named log is
     * the correct outcome - the alternative is every older client crashing the moment a new
     * RPC is added, which is precisely what the phone work is about to do.
     */
    if (!pContext)
    {
        // Once per id per connection - see m_refusedIds. A repeating push to an older
        // client would otherwise log this on every send, for every player.
        if (m_refusedIds.insert(id).second)
        {
            spdlog::warn("Rpc id {} has no handler on this client (class {:X}, function {:X}) - "
                         "refusing it, and staying quiet about this id from here. This build is "
                         "likely older than the server.",
                         id, rpc.Id.Klass, rpc.Id.Function);
        }

        return false;
    }

    const auto pFunc = pContext->function;
    const auto combinedArgCount = pFunc->params.size;

    static auto* cScriptContext = static_cast<RED4ext::IScriptable*>(Red::GetClass("entEntity")->CreateInstance());

    Red::CStack stack(cScriptContext);
    Red::StackArgs_t args;

    if (combinedArgCount > 0)
    {
        ViewBuffer buffer(const_cast<uint8_t*>(aMessage.get_arguments().data()), aMessage.get_arguments().size());
        Buffer::Reader reader(&buffer);
        for (auto i = 0; i < combinedArgCount; ++i)
        {
            auto* pType = pFunc->params[i]->type;
            const auto* pTypeAllocator = pType->GetAllocator();

            auto* pInstance = pTypeAllocator->AllocAligned(pType->GetSize(), pType->GetAlignment()).memory;
            std::memset(pInstance, 0, pType->GetSize());
            pType->Construct(pInstance);

            auto& arg = args.emplace_back();
            arg.value = pInstance;
            arg.type = pType;

            pContext->context.args[i].pack->Read(args.back().value, reader);
        }

        stack.args = args.data();
        stack.argsCount = pFunc->params.size;
    }

    Red::Detail::CallFunctionWithStack(nullptr, pFunc, stack);

    for (const auto& arg : args)
    {
        const auto* pType = arg.type;
        const auto* pTypeAllocator = pType->GetAllocator();

        pType->Destruct(arg.value);
        pTypeAllocator->Free(arg.value);
    }

    return true;
}

