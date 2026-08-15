import CyberpunkMP.Plugins.*

public native class ChatMessageUIEvent extends Event {
    public native let author: String;
    public native let message: String;

    // Mirrors the field of the same name on the C++ ChatMessageUIEvent. Every native
    // field has to be declared on BOTH sides - native here means "the storage lives in
    // C++", not "redscript will work it out".
    public native let channel: Uint32;
}

public class ConnectedToServer extends Event {
    public let m_connected: Bool;
}

// The server asking what this character is called.
//
// Plain redscript, not native like ChatMessageUIEvent above - the C++ side only calls a
// method on NetworkWorldSystem and that method raises this, so nothing about the event
// itself has to exist in both languages. One less thing that can be declared on one side
// and not the other.
public class CharacterNameRequest extends Event {
    public let m_current: String;
}

public class ChatMessageData extends IScriptable {
    public let m_author: String;
    public let m_message: String;
    public let m_isSelf: Bool;
    public let m_needsAuthorLabel: Bool = true;

    // Matches ChatChannel on the server: 0 local, 1 yell, 2 whisper, 3 advert, 4 server.
    public let m_channel: Uint32 = 0u;
}

// The colour each chat channel is drawn in.
//
// Kept here rather than inside the widget so it is one list in one place, and so the
// values can be read next to each other - which is the only way to tell whether they are
// actually distinguishable.
//
// Local is left alone: it is the overwhelming majority of what anyone says, and tinting
// the common case makes the rare ones stop standing out.
public func GetChatChannelColor(channel: Uint32) -> HDRColor {
    switch channel {
        case 1u: return new HDRColor(2.0, 0.30, 0.30, 1.0);   // yell - red
        case 2u: return new HDRColor(1.65, 0.55, 1.90, 1.0);  // whisper - pink-purple
        case 3u: return new HDRColor(2.0, 1.75, 0.25, 1.0);   // advert - yellow
    }

    // Local and SERVER notices keep the widget's own styling.
    return new HDRColor(1.0, 1.0, 1.0, 1.0);
}

public func ChatChannelIsTinted(channel: Uint32) -> Bool {
    return channel == 1u || channel == 2u || channel == 3u;
}

public class ServerData extends IScriptable {
    public let m_name: String;
    public let m_description: String;
    public let m_test: Bool = false;
}

public class JobType extends IScriptable {
    public let m_name: String;
    public let m_description: String;
    public let m_test: Bool = false;
}

public class ItemData extends IScriptable {
    public let m_entry: ShopEntry;
}

public class EmoteData extends IScriptable {
    public let m_name: String;
}

public func CreateInputHint(label: CName, action: CName, hold: Bool) -> InputHintData {
    let data: InputHintData;
    data.source = n"CyberpunkMP";
    data.action = action;
    if hold {
        data.holdIndicationType = inkInputHintHoldIndicationType.Hold;
    } else {
        data.holdIndicationType = inkInputHintHoldIndicationType.Press;
    }
    data.sortingPriority = 0;
    data.enableHoldAnimation = false;
    data.localizedLabel = GetLocalizedTextByKey(label);
    if StrLen( data.localizedLabel) == 0 {
            data.localizedLabel = ToString(label);
    };
    // data.groupId = n"CyberpunkMP";
    return data;
}