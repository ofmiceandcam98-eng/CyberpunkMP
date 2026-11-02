#include "RpcValidator.h"

#include <xbyak/xbyak.h>

#include "RpcGenerator.h"
#include "App/World/RpcDefinition.h"
#include "Core/Hooking/HookingAgent.hpp"
#include "Game/ScriptValidation.h"

bool ValidateType(const Red::CBaseRTTIType* pType, Red::CClass* klass)
{
    static auto s_floatType = Red::ResolveType<float>()->GetName();
    static auto s_int32Type = Red::ResolveType<int32_t>()->GetName();
    static auto s_uint32Type = Red::ResolveType<uint32_t>()->GetName();
    static auto s_int64Type = Red::ResolveType<int64_t>()->GetName();
    static auto s_uint64Type = Red::ResolveType<uint64_t>()->GetName();
    static auto s_nameType = Red::ResolveType<Red::CName>()->GetName();
    static auto s_doubleType = Red::ResolveType<double>()->GetName();
    static auto s_stringType = Red::ResolveType<Red::CString>()->GetName();
    static auto s_boolType = Red::ResolveType<bool>()->GetName();
    static auto s_tweakdbId = Red::ResolveType<Red::TweakDBID>()->GetName();

    if (pType == nullptr)
        return true;

    if (pType->GetName() == s_floatType || pType->GetName() == s_int32Type || pType->GetName() == s_uint32Type || pType->GetName() == s_int64Type ||
        pType->GetName() == s_uint64Type || pType->GetName() == s_nameType || pType->GetName() == s_doubleType || pType->GetName() == s_stringType ||
        pType->GetName() == s_boolType || pType->GetName() == s_tweakdbId)
        return true;

    if (pType->GetType() == Red::ERTTIType::Class)
    {
        auto* pClass = (Red::CClass*)pType;
        if (!ValidateType(pClass->parent, klass))
            return false;

        Red::DynArray<Red::CProperty*> props;
        pClass->GetProperties(props);

        for (const auto prop : props)
        {
            if (!ValidateType(prop->type, pClass))
                return false;
        }

        return true;
    }

    if (pType->GetType() == Red::ERTTIType::Array || pType->GetType() == Red::ERTTIType::FixedArray || pType->GetType() == Red::ERTTIType::NativeArray || pType->GetType() == Red::ERTTIType::StaticArray)
    {
        auto* pClass = (Red::CRTTIBaseArrayType*)pType;
        if (!ValidateType(pClass->innerType, klass))
            return false;

        return true;
    }

    return false;
}

bool ValidateType(Set<RED4ext::CName>& knownTypes, Game::ScriptType* type, Game::ScriptClass* klass, Game::ScriptFunction* func, Game::ScriptReport* apReporter)
{
    if (type == nullptr)
        return true;

    if (type->metaType == Game::EScriptType::ScriptRef ||
        type->metaType == Game::EScriptType::StrongHandle ||
        type->metaType == Game::EScriptType::WeakHandle)
    {
        auto res = std::format(
            "CyberpunkMP: RpcDefinition '{}' function '{}' type '{}' is a handle which is not supported.", klass->name.ToString(), func->name.ToString(),
            type->name.ToString());
        Game::StringView v;
        v.length = res.size();
        v.str = res.c_str();

        apReporter->AddValidationError(v);
        return false;
    }

    if (type->metaType == Game::EScriptType::DynArray ||
        type->metaType == Game::EScriptType::StaticArray)
    {
        return ValidateType(knownTypes, static_cast<Game::ScriptType*>(type->innerType), klass, func, apReporter);    
    }

    //if (type->metaType == Game::EScriptType::Primitive) -> this is always valid
    if (type->metaType == Game::EScriptType::Class)
    {
        if (const auto loc = knownTypes.find(type->name); loc != std::end(knownTypes))
            return true;

        const auto pClass = reinterpret_cast<Game::ScriptClass*>(type->innerType);
        if (!ValidateType(knownTypes, reinterpret_cast<Game::ScriptType*>(pClass->parent), klass, func, apReporter))
            return false;

        auto* pBaseClass = RED4ext::CRTTISystem::Get()->GetClassByScriptName(type->innerType->name);
        if (!pBaseClass)
            return !pClass->flags.isNative;

        for(const auto pProp : pClass->properties)
        {
            const auto* pBaseProp = pBaseClass->GetProperty(pProp->name);
            if (!pBaseProp)
                return false;

            if (!ValidateType(pBaseProp->type, pBaseClass))
                return false;
        }

        knownTypes.insert(type->name);
    }

    return true;
}

// ScriptFunction past `name` is Codeware's 2.5 decode with every field shifted by the 0x28 that
// ScriptDefinition grew - but Codeware asserts none of it, and ScriptClass's own 2.5 growth was NOT
// a uniform shift. So the layout is checked against back-pointers before anything is trusted:
// a misread there is a silent "Missing native function" for every RPC, which is how 2.5 test.67
// failed with nothing in our log to say why.
bool LayoutLooksRight(const Game::ScriptClass* klass, const Game::ScriptFunction* func)
{
    if (func->parent != klass)
        return false;

    if (func->params.size() > 64)
        return false;

    for (const auto param : func->params)
    {
        if (!param || param->function != func)
            return false;
    }

    return true;
}

bool IsNativeFunction(const Game::ScriptClass* klass, const Game::ScriptFunction* func)
{
    if (func->flags.isNative)
        return true;

    // A ServerRpc subclass is a native class of static native functions by construction; the
    // class flag (0xD1) is asserted by Codeware, the function flag (0xC0) is not.
    if (klass->flags.isNative && LayoutLooksRight(klass, func))
    {
        spdlog::warn(
            "[Rpc] {}::{} - isNative bit at 0xC0 reads clear (raw flags 0x{:08X}) in a native class; trusting the class",
            klass->name.ToString(), func->name.ToString(), *reinterpret_cast<const uint32_t*>(&func->flags));
        return true;
    }

    return false;
}

bool ValidateFunction(Game::ScriptClass* klass, Game::ScriptFunction* func, Game::ScriptReport* apReporter)
{
    if (!IsNativeFunction(klass, func))
    {
        spdlog::error(
            "[Rpc] {}::{} skipped - not native (raw flags 0x{:08X}, layout {}) - the game will report it as a missing native function",
            klass->name.ToString(), func->name.ToString(), *reinterpret_cast<const uint32_t*>(&func->flags),
            LayoutLooksRight(klass, func) ? "ok" : "MISREAD");
        return false;
    }

    Set<RED4ext::CName> knownTypes;

    for (const auto param : func->params)
    {
        if (param->flags.isOut || param->flags.isOptional)
        {
            auto res = std::format(
                "CyberpunkMP: RpcDefinition '{}' function '{}' parameter '{}' is out or optional which is invalid.", klass->name.ToString(), func->name.ToString(),
                param->name.ToString());
            Game::StringView v;
            v.length = res.size();
            v.str = res.c_str();

            apReporter->AddValidationError(v);
            return false;
        }

        if (!ValidateType(knownTypes, param->type, klass, func, apReporter))
        {
            spdlog::error(
                "[Rpc] {}::{} skipped - parameter '{}' has a type the RPC layer cannot carry", klass->name.ToString(),
                func->name.ToString(), param->name.ToString());
            return false;
        }
    }

    return true;
}

bool (*RealScriptValidator_Validate)(void* apThis, Game::ScriptBundle* apBundler, Game::ScriptReport* apReporter);
bool HookScriptValidator_Validate(void* apThis, Game::ScriptBundle* apBundler, Game::ScriptReport* apReporter)
{
    apReporter->fillErrors = true;

    constexpr Red::CName parentName("CyberpunkMP.ServerRpc");
    uint32_t rpcClasses = 0;
    for (const auto klass : apBundler->classes)
    {
        if (klass->parent && klass->parent->name == parentName)
        {
            ++rpcClasses;
            const auto pClass = Red::GetClass(klass->name);
            spdlog::info(
                "[Rpc] validator: {} ({} functions, class native={}) {}", klass->name.ToString(), klass->functions.size(),
                static_cast<bool>(klass->flags.isNative), pClass ? "ALREADY IN RTTI - left as is" : "registering");

            if (!pClass)
            {
                using Descriptor = Red::ClassDescriptorDefaultImpl<RpcMock>;
                const auto pDesc = new Descriptor();
                pDesc->name = klass->name;
                pDesc->flags.isNative = 1;
                pDesc->flags.isScriptedClass = 1;
                pDesc->SetParent<ServerRpc>();

                for (const auto func : klass->functions)
                {
                    if (!ValidateFunction(klass, func, apReporter))
                        continue;

                    RpcGenerator::CreateFunction(pDesc, klass, func);
                }

                auto* rtti = Red::CRTTISystem::Get();
                rtti->RegisterType(pDesc);
            }
        }
    }

    // Zero means the walk itself is misreading the bundle (classes / parent / name offsets), not
    // that a function was rejected - the two need very different fixes.
    spdlog::info("[Rpc] validator: {} classes in bundle, {} extend CyberpunkMP.ServerRpc", apBundler->classes.size(), rpcClasses);

    return RealScriptValidator_Validate(apThis, apBundler, apReporter);
}

constexpr auto ScriptValidator_Validate = Core::RawFunc<
    /* hash = */ 898639042,
    /* type = */ decltype(RealScriptValidator_Validate)>();

void RpcValidator::InternalValidate()
{
    for(auto func : RpcGenerator::GetFunctions())
    {
        for (const auto param : func->params)
        {
            if (!ValidateType(param->type, func->parent))
            {
                spdlog::error(
                    "Function {} in Class {} uses {} for arg {} which is invalid!", func->fullName.ToString(), func->parent->name.ToString(), param->type->GetName().ToString(),
                    param->name.ToString());
            }
        }
    }

    auto* pParent = Red::GetClass("ClientRpc");
    RED4ext::CRTTISystem::Get()->types.for_each(
        [pParent](const Red::CName& name, Red::CBaseRTTIType* type)
        {
            if(type->GetType() == Red::ERTTIType::Class)
            {
                const auto* pClass = static_cast<Red::CClass*>(type);
                if (pClass->IsA(pParent))
                {
                    for (const auto func : pClass->funcs)
                    {
                        for (const auto param : func->params)
                        {
                            if (!ValidateType(param->type, func->parent))
                            {
                                spdlog::error(
                                    "Function {} in Class {} uses {} for arg {} which is invalid!", func->fullName.ToString(), func->parent->name.ToString(),
                                    param->type->GetName().ToString(), param->name.ToString());
                            }
                        }
                    }
                }
            }
        });
}

void RpcValidator::Attach()
{
    Hook<ScriptValidator_Validate>(&HookScriptValidator_Validate, &RealScriptValidator_Validate);
}
