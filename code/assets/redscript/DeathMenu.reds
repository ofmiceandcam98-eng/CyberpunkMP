// The OnInitialize wrap that used to live here has moved into Death.reds.
//
// Two @wrapMethod hooks on the same method in two files chain in an order redscript does
// not define. That mattered here: the suppression wrap returns without chaining, so if
// this file happened to run first it carried on using m_buttonHintsManagerRef from a
// controller the vanilla OnInitialize had never built. One wrap, one place, no ordering to
// reason about.

@addMethod(DeathMenuGameController)
protected cb func OnChatSpawn(widget: ref<inkWidget>, userData: ref<IScriptable>) -> Bool {
    widget.SetAnchorPoint(1.0, 1.0);
}
