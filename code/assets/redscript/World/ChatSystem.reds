module CyberpunkMP.World

import Codeware.*
import CyberpunkMP.*

public native class ChatSystem extends IScriptable {
    public native func Send(message: String);
    public native func GetUsername() -> String;
    // Persist the trade overlay placement (x/y fraction, zoom, gap) to a file so it survives
    // relaunch. LoadTradePlacement returns (0,0,0,0) when nothing is saved.
    public native func SaveTradePlacement(x: Float, y: Float, zoom: Float, gap: Float) -> Void;
    public native func LoadTradePlacement() -> Vector4;

    public func HandleChatMessage(author: String, message: String) {
        let evt: ref<ChatMessageUIEvent>;
        evt.author = author;
        evt.message = message;

        LogChannel(n"DEBUG", s"[ChatSystem] HandleChatMessage: " + message);
        let uiSystem = GameInstance.GetUISystem(GetGameInstance());        
        uiSystem.QueueEvent(evt);
    }
    // private final func OnConnectToServer(request: ref<ConnectToServerRequest>) -> Void {
        // this.m_Blackboard.SetBool(GetAllBlackboardDefs().UI_ComDevice.ContactsActive, open, true);
    // }
}
