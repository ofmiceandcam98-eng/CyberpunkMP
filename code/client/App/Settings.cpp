#include "Settings.h"
#include <RED4ext/LaunchParameters.hpp>

void Settings::Load()
{
    Settings& settings = Get();

    auto& launchParameters = RED4ext::GetLaunchParameters();

    if (launchParameters.Contains(RED4ext::CString("-online")))
        settings.enabled = true;

    // Developer overlay. The launcher only offers this to accounts with a dev or admin
    // Discord role, but nothing here enforces that - every action the overlay exposes is
    // re-checked by the server against the role IT derived from Discord. This flag decides
    // whether a toolbar is drawn, nothing more.
    if (launchParameters.Contains(RED4ext::CString("-debug")))
        settings.debug = true;

    // Voice, from the launcher's Settings > Voice page.
    //
    // Every one has a working default, so a game started without the launcher - or by an
    // older launcher that does not send these - still has a usable voice configuration
    // rather than no key and silence.
    if (const auto key = launchParameters.Get("-voicekey"); key && key->size > 0)
        settings.voicePushToTalkKey = (*key)[0].c_str();

    if (const auto rangeKey = launchParameters.Get("-voicerangekey"); rangeKey && rangeKey->size > 0)
        settings.voiceCycleRangeKey = (*rangeKey)[0].c_str();

    if (const auto mode = launchParameters.Get("-voicemode"); mode && mode->size > 0)
        settings.voiceMode = (*mode)[0].c_str();

    if (const auto mic = launchParameters.Get("-micvolume"); mic && mic->size > 0)
    {
        // Clamped again here. The launcher clamps before saving, but this is a launch
        // argument - anything can pass one - and a gain read straight from an argument is
        // a scream waiting to happen.
        const auto value = std::strtoul((*mic)[0].c_str(), nullptr, 10);
        settings.voiceMicVolume = static_cast<uint32_t>(value > 200 ? 200 : value);
    }

    if (const auto chat = launchParameters.Get("-voicevolume"); chat && chat->size > 0)
    {
        const auto value = std::strtoul((*chat)[0].c_str(), nullptr, 10);
        settings.voiceChatVolume = static_cast<uint32_t>(value > 200 ? 200 : value);
    }

    if (const auto in = launchParameters.Get("-voicein"); in && in->size > 0)
        settings.voiceInputDevice = (*in)[0].c_str();

    if (const auto out = launchParameters.Get("-voiceout"); out && out->size > 0)
        settings.voiceOutputDevice = (*out)[0].c_str();

    bool ipFromArgs = false;
    bool portFromArgs = false;

    if (const auto ip = launchParameters.Get("-ip"); ip)
    {
        if (ip->size > 0)
        {
            settings.ip = (*ip)[0].c_str();
            ipFromArgs = true;
        }
    }

    if (const auto port = launchParameters.Get("-port"); port)
    {
        if (port->size > 0)
        {
            settings.port = std::strtoul((*port)[0].c_str(), nullptr, 10) & 0xFFFF;
            portFromArgs = true;
        }
    }

    if (const auto token = launchParameters.Get("-discord-token"); token)
    {
        if (token->size > 0)
            settings.discordToken = (*token)[0].c_str();
    }

    if (const auto name = launchParameters.Get("-discord-name"); name)
    {
        if (name->size > 0)
            settings.discordName = (*name)[0].c_str();
    }

    // The manifest attestation trio - see Settings.h. Carried, never computed here.
    if (const auto manifest = launchParameters.Get("-manifest-version"); manifest && manifest->size > 0)
        settings.manifestVersion = (*manifest)[0].c_str();

    if (const auto digest = launchParameters.Get("-install-digest"); digest && digest->size > 0)
        settings.installDigest = (*digest)[0].c_str();

    if (const auto unmanaged = launchParameters.Get("-unmanaged"); unmanaged && unmanaged->size > 0)
    {
        // One comma-joined argument rather than a repeated flag, because the launcher
        // builds one argv and the list is small (the launcher caps it before sending).
        const std::string joined = (*unmanaged)[0].c_str();
        size_t start = 0;
        while (start < joined.size())
        {
            auto end = joined.find(',', start);
            if (end == std::string::npos)
                end = joined.size();
            if (end > start)
                settings.unmanaged.push_back(String(joined.substr(start, end - start).c_str()));
            start = end + 1;
        }
    }

    if (const auto password = launchParameters.Get("-server-password"); password && password->size > 0)
        settings.serverPassword = (*password)[0].c_str();

    spdlog::info("Manifest: {}", settings.manifestVersion.empty()
                                     ? "none - launched without the launcher, or pre-manifest launcher"
                                     : fmt::format("{} (digest {}, {} unmanaged reported)",
                                                   settings.manifestVersion.c_str(),
                                                   settings.installDigest.empty() ? "MISSING" : "present",
                                                   settings.unmanaged.size()));

    // Which record remote players are built from. See Settings.h - this exists so the
    // record can be changed between launches instead of between releases, because
    // finding one that is both stable and targetable is trial and error that costs two
    // people being online for every attempt.
    if (const auto record = launchParameters.Get("-puppet-record"); record)
    {
        if (record->size > 0)
        {
            settings.puppetRecordMale = (*record)[0].c_str();
            settings.puppetRecordFemale = (*record)[0].c_str();
        }
    }

    if (launchParameters.Get("-puppet-driver-all"))
        settings.puppetDriverAll = true;

    if (const auto record = launchParameters.Get("-puppet-record-female"); record)
    {
        if (record->size > 0)
            settings.puppetRecordFemale = (*record)[0].c_str();
    }

    spdlog::info("Puppet records: male {} / female {}", settings.puppetRecordMale.c_str(),
                 settings.puppetRecordFemale.c_str());

    spdlog::info("Display name: {}", settings.discordName.empty() ? "none - not launched from the launcher"
                                                                 : settings.discordName.c_str());

    // Never log the token itself - it is a live credential, and these logs get pasted
    // into Discord by people reporting crashes. Length is enough to tell "present" from
    // "the launcher passed nothing".
    spdlog::info("Discord token: {}", settings.discordToken.empty()
                                          ? "none - launched without the launcher"
                                          : fmt::format("present ({} chars)", settings.discordToken.length()));

    // Report what we actually parsed. The game's parser only fills these in for the
    // --ip=<addr> / --port=<n> form; "-ip <addr>" produces an empty value list and
    // silently leaves the defaults in place, which looks identical to a dead server.
    spdlog::info("Server address: {}:{} (ip {}, port {})", settings.ip, settings.port,
                 ipFromArgs ? "from launch args" : "DEFAULT - --ip= was not parsed",
                 portFromArgs ? "from launch args" : "DEFAULT - --port= was not parsed");

    if (const auto mods = launchParameters.Get("-mod"); mods)
    {
        for (const auto& mod : *mods)
            settings.mods.push_back(mod.c_str());
    }

    if (launchParameters.Contains("-rpc"))
    {
        settings.RpcOnly = true;
    }

    if (const auto rpcDir = launchParameters.Get("-rpcdir"); rpcDir)
    {
        if (rpcDir->size > 0)
        {
            // For some reason cyberpunk adds a \ at the start and end of the path...
            std::string path = std::string((*rpcDir)[0].c_str());
            path = path.substr(1, path.length() - 2);

            settings.RpcPath = path;
        }
    }
}
