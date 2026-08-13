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
    Vector<fs::path> mods = {};
    bool enabled = false;
    bool RpcOnly = false;
    fs::path RpcPath{};

private:
    Settings() = default;
};