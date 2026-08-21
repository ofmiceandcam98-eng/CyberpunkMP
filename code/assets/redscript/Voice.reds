module CyberpunkMP

/**
 * How a player chooses to key their microphone.
 *
 * The state and the logic live on MultiplayerGameController, declared there directly
 * rather than bolted on with @addMethod - it is our own class, in our own module, so
 * there is nothing to annotate onto it. (Annotating a class from inside its own module
 * fails as UNRESOLVED_REF pointing at the annotation, which reads like the class is
 * missing rather than like the annotation is unnecessary.)
 *
 * The enum lives here on its own so the voice vocabulary has one home as the rest of the
 * system arrives - capture, encoding and routing all need to agree on these three modes.
 */
public enum MpVoiceMode {
    // Hold the key to talk. The default, deliberately: voice activation transmits whatever
    // the room is doing - a fan, a keyboard, someone else's television - to everyone
    // standing nearby, and the person doing it is always the last to find out.
    PushToTalk = 0,

    // Press once to open the microphone, press again to close it. Latches, which is why
    // anything that ends a session has to stop it explicitly.
    Toggle = 1,

    // The microphone opens itself when the input is loud enough. Needs the capture layer
    // to have a level to threshold against, so it does nothing yet.
    VoiceActivation = 2,
}
