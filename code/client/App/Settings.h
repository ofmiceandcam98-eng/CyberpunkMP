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
    // Shows the ImGui debug overlay - the "Test" menu bar. Off unless the launcher passes
    // --debug, which it only offers to accounts with a dev or admin Discord role.
    //
    // This is convenience, not security: anyone can add a launch flag by hand, and the
    // menu only exposes actions the SERVER independently checks permission for. It exists
    // so ordinary players are not confronted with a developer toolbar over their game.
    bool debug = false;

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
    Vector<fs::path> mods = {};
    bool enabled = false;
    bool RpcOnly = false;
    fs::path RpcPath{};

private:
    Settings() = default;
};