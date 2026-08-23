#pragma once

namespace fs = std::filesystem;

struct Settings
{
    static Settings& Get()
    {
        static Settings instance;
        return instance;
    }
    static bool IsDisabled()
    {
        return !Get().enabled;
    }
    static void Load();

    fs::path exePath{};
    fs::path gamePath{};
    String Version{};

    // ---------------------------------------------------------------------------
    // Voice, chosen in the launcher and handed over at launch.
    //
    // Owned by the launcher rather than the game because that is where a person can
    // actually change them - a key capture and two sliders in a settings page, instead of
    // a rebind screen that a custom input action is not guaranteed to appear in.
    //
    // Read once at startup, like every other launch parameter. Changing them takes effect
    // next launch; live changes would need a file the mod can re-read.
    // ---------------------------------------------------------------------------

    // The game's own key id - IK_V, IK_Mouse4, IK_CapsLock. Only used to bind the
    // VoicePushToTalk ACTION; nothing downstream ever compares against it.
    String voicePushToTalkKey{"IK_V"};

    // Cycles whisper -> local -> yell. A separate binding from push-to-talk on purpose:
    // one decides WHEN somebody talks, the other HOW FAR it carries, and sharing a key
    // would mean every range change also put them on air.
    String voiceCycleRangeKey{"IK_F12"};

    // "ptt", "toggle" or "activation".
    String voiceMode{"ptt"};

    // Percentages where 100 means unchanged. Above 100 is allowed on purpose: a quiet
    // microphone on a professional interface is normal, and the alternative is telling
    // somebody to go and change their hardware gain.
    uint32_t voiceMicVolume{100};
    uint32_t voiceChatVolume{100};

    // Windows endpoint ids. Empty means follow the Windows default, which is what somebody
    // means when they have not chosen deliberately.
    String voiceInputDevice{};
    String voiceOutputDevice{};
    String ip = "127.0.0.1";
    uint16_t port = 11778;
    // Discord OAuth access token, handed over by the launcher via --discord-token=.
    // Passed straight to the server, which asks Discord who it belongs to. The client
    // never inspects or asserts anything about it - it is a bearer credential in transit.
    String discordToken{};
    // Display name from the launcher. Used only while server-side Discord
    // verification is off; once it is on, the server replaces this with the name
    // Discord actually returns.
    String discordName{};

    // ---------------------------------------------------------------------------
    // Manifest attestation, handed over by the launcher via launch arguments and
    // forwarded verbatim in the authentication request. The client neither computes
    // nor inspects any of it - the launcher verified the installation, the server
    // holds the expectations, and this is just the courier between them. All empty
    // when the game was started without the launcher, which the server treats as
    // "unverified" (denied once the migration window closes).
    // See docs/MANIFEST-ARCHITECTURE.md sections 7.1-7.2.
    // ---------------------------------------------------------------------------

    // Which manifest the launcher verified this installation against (--manifest-version=).
    String manifestVersion{};

    // The launcher's install attestation - 64 lowercase hex chars (--install-digest=).
    // Hex rather than base64 deliberately: the game's own argument tokenizer is a black
    // box and '='/'/' are known hazards in it; [0-9a-f] cannot collide with anything.
    String installDigest{};

    // Unmanaged findings from the launcher's scan, type-prefixed, comma-joined in the
    // argument and split here (--unmanaged=plugin:X,archive:Y.archive).
    Vector<String> unmanaged{};

    // Only sent when the server config sets a password (--server-password=).
    String serverPassword{};
    // Shows the ImGui debug overlay - the "Test" menu bar. Off unless the launcher passes
    // --debug, which it only offers to accounts with a dev or admin Discord role.
    //
    // This is convenience, not security: anyone can add a launch flag by hand, and the
    // menu only exposes actions the SERVER independently checks permission for. It exists
    // so ordinary players are not confronted with a developer toolbar over their game.
    bool debug = false;

    // Dev-only movement tracing for tools/netlab: one NDJSON line per received movement
    // sample and per applied pose, written to the mod's logs folder (so it ships to the
    // server with the rest). Off by default and never offered by the launcher - this is
    // for capturing a real session's network behaviour to replay through candidate
    // interpolation algorithms, not for players.
    bool syncTrace = false;

    // Which TweakDB record remote players are built from.
    //
    // Configurable because it is a question nobody can answer from reading code. The
    // muppet records are mannequins - a body and animations, and none of what Cyberpunk
    // needs to treat something as a target, which is why you cannot aim at another
    // player. Records that DO carry health and hit detection may also drag in behaviour
    // that has no business on a puppet the server is driving.
    //
    // Finding the right one is experiment, and each experiment costs two people being
    // online at once. A launch flag turns that into "relaunch and try the next one"
    // rather than a rebuild and a release per guess.
    //
    // Candidates worth trying: Character.Player_Puppet_Base (what the real player is
    // built from), Character.MaMuppet (the mannequin, no targeting).
    //
    // The mannequins are the PLAYABLE baseline: they move, they sync, every world
    // feature runs on them. Player_Puppet_Base (real faces) went through two live
    // rounds - round 1 proved identity but froze (no controller), round 2 attached
    // the new PuppetDriver but the transform writes still did not visibly move them.
    // The real-rig work continues OFF the test channel (-puppet-record flag) until it
    // demonstrably walks; player sessions stay on what works.
    String puppetRecordMale = "Character.MaMuppet";
    String puppetRecordFemale = "Character.WaMuppet";

    // Route EVERY puppet through the mod-owned PuppetDriver (movement by placed
    // transform, animation by feature writes) instead of only player-record puppets.
    // The A/B lever for retiring the legacy idle-controller hijack.
    bool puppetDriverAll = false;

    // Apply the GameplayRestriction.FistFight status effect to remote puppets, which is
    // what ScriptedPuppet.IsAggressive() reads first - one of the two remaining obstacles
    // to a remote player being a legitimate quickhack target. See Hackable.reds.
    //
    // A flag rather than simply on, because FistFight is the brawl restriction and the game
    // consults it in melee stim handling as well. On a server-driven puppet that never
    // swings at anything it should be inert; "should be" is not "is", and this way turning
    // it off is a relaunch instead of a rebuild.
    bool hackablePuppets = false;
    Vector<fs::path> mods = {};
    bool enabled = false;
    bool RpcOnly = false;
    fs::path RpcPath{};

private:
    Settings() = default;
};