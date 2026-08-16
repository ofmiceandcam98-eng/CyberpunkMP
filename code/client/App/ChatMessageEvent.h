#pragma once

#include <RED4ext/Scripting/Natives/Generated/red/Event.hpp>
#include <RED4ext/CString.hpp>
#include "Red/TypeInfo/Macros/Definition.hpp"

struct ChatMessageUIEvent : RED4ext::red::Event
{
    RED4ext::CString author;
    RED4ext::CString message;

    // Which channel the SERVER said this was on - see ChatChannel on the server side.
    // Drives the colour the line is drawn in.
    uint32_t channel{0};

    RTTI_IMPL_TYPEINFO(ChatMessageUIEvent);
    RTTI_IMPL_ALLOCATOR();
};

RTTI_DEFINE_CLASS(ChatMessageUIEvent, {
    RTTI_PARENT(RED4ext::red::Event);
    // auto pType = Red::GetClass("String");
    // type->props.PushBack(Red::CProperty::Create(pType, "author", nullptr, static_cast<uint32_t>(0x40)));
    // type->props.PushBack(Red::CProperty::Create(pType, "message", nullptr, static_cast<uint32_t>(0x60)));
    RTTI_PROPERTY(author);
    RTTI_PROPERTY(message);
    RTTI_PROPERTY(channel);
});