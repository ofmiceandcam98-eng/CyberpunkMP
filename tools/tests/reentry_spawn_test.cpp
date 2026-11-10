// A SPAWN IS OWED PER WORLD, NOT PER CONNECTION. AND WHAT THE SERVER DECIDES HAS TO LAND.
//
// Found in one solo session on 2026-11-08, test.94. Each of these failed in silence: nothing
// errored, every layer reported success, and the player was simply somewhere the server
// did not think they were.
//
//   1. Back to the selector, PLAY again on the same connection: the server had released the
//      puppet and the client sent no new spawn, because a spawn was only ever held when the
//      connection was made. The player stood in the city with no body on the server, so
//      nothing saved and /respawn answered "You are not in the world yet".
//   2. /kill reached the client and nothing happened: the poll that applies what the server
//      decided was armed only from OnConnected, and connecting now happens at the main menu,
//      before there is a player. Bound to the player's attach instead.
//   3. An access violation while the mod loaded left only "[Boot] logger up", because the
//      crash reporter written for exactly that window was installed after it.
//
// These live in the client DLL and in redscript, which cannot be loaded here without the
// game, so this checks the SOURCE for the sentences a regression has to delete. It is a
// guard against somebody tidying these away, not a proof that they work - that was a live
// session, and the Atlas branches record it.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static int s_failures = 0;
static int s_checks = 0;

static void Check(bool aCondition, const char* acpWhat)
{
    ++s_checks;
    if (aCondition)
    {
        std::printf("ok    %s\n", acpWhat);
    }
    else
    {
        ++s_failures;
        std::printf("FAIL  %s\n", acpWhat);
    }
}

static std::string ReadRepoFile(const char* acpRelative)
{
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    std::ifstream file(root / acpRelative, std::ios::binary);
    if (!file)
    {
        std::printf("FAIL  cannot open %s - the file moved, so this guard no longer guards it\n", acpRelative);
        ++s_failures;
        return {};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// The text from a function's signature to the next function at column zero.
static std::string BodyOf(const std::string& acSource, const char* acpSignature)
{
    const auto start = acSource.find(acpSignature);
    if (start == std::string::npos)
        return {};

    auto end = acSource.find("\nvoid ", start + 1);
    if (end == std::string::npos)
        end = acSource.size();

    return acSource.substr(start, end - start);
}

int main()
{
    const auto world = ReadRepoFile("code/client/App/World/NetworkWorldSystem.cpp");
    const auto service = ReadRepoFile("code/client/App/Network/NetworkService.cpp");
    const auto mainCpp = ReadRepoFile("code/client/Main.cpp");
    const auto combat = ReadRepoFile("code/assets/redscript/Combat.reds");
    const auto death = ReadRepoFile("code/assets/redscript/Death.reds");
    const auto worldScript = ReadRepoFile("code/assets/redscript/World/NetworkWorldSystem.reds");

    // ---------------------------------------------------------------- 1. RE-ENTRY
    {
        const auto leave = BodyOf(world, "void NetworkWorldSystem::LeaveWorld()");
        Check(leave.find("LeaveWorldRequest") != std::string::npos, "LeaveWorld still tells the server");
        Check(leave.find("HoldSpawnUntilEntry") != std::string::npos,
              "LeaveWorld holds a new spawn - the server just destroyed the body, so the next PLAY owes one");

        const auto hold = BodyOf(service, "void NetworkService::HoldSpawnUntilEntry()");
        Check(hold.find("m_spawnDeferred = true") != std::string::npos, "the held spawn is marked as owed");
        Check(hold.find("m_spawnAuthorized = false") != std::string::npos,
              "and it waits for PLAY rather than going out by itself");

        const auto enter = BodyOf(world, "void NetworkWorldSystem::EnterWorld()");
        Check(enter.find("IsSpawnDeferred") != std::string::npos, "EnterWorld still sends only a spawn that is owed");
    }

    // ---------------------------------------------------------------- 2. NOTHING STREAMS FOR A BODY THAT IS GONE
    {
        Check(world.find("!GetRemotePlayerId() || m_leftWorld") != std::string::npos,
              "movement stops once the player has left the world");
        Check(world.find("m_leftWorld = false") != std::string::npos, "and starts again when a spawn answers");
    }

    // ---------------------------------------------------------------- 3. WHAT THE SERVER DECIDES LANDS
    {
        const auto attached = death.find("OnGameAttached");
        Check(attached != std::string::npos && death.find("MpArmStatusPoll(this)", attached) != std::string::npos,
              "the status poll is armed when the player attaches, which is the only moment a menu connect has one");
        Check(worldScript.find("new MpStatusEffectPoll()") == std::string::npos,
              "OnConnected no longer starts its own chain - one poll per player, through MpArmStatusPoll");
        Check(combat.find("m_mpStatusPollArmed") != std::string::npos, "arming is idempotent for a given player");
        Check(combat.find("next.owner = this.owner") != std::string::npos,
              "a chain stays bound to its player, so it ends with that player's world");
        Check(combat.find("MpApplyDownedState(player, network)") != std::string::npos,
              "the poll still applies being put down");
    }

    // ---------------------------------------------------------------- 4. THE CRASH REPORTER COVERS THE LOAD
    {
        const auto reporter = mainCpp.find("InstallCrashReporter();");
        const auto application = mainCpp.find("MakeUnique<App::Application>");
        Check(reporter != std::string::npos && application != std::string::npos && reporter < application,
              "the crash reporter is installed before anything in the mod's load can fault");
    }

    // ---------------------------------------------------------------- 5. THE OVERLAY DRAWS
    // ImGui is not pinned, and 1.92 stopped filling ImDrawData::CmdListsCount. Every reader saw
    // zero, so the overlay built its bar and handed the renderer nothing, with no error at all.
    {
        const auto backend = ReadRepoFile("code/client/App/Rendering/dx12.cpp");
        const auto overlay = ReadRepoFile("code/client/App/Rendering/ImGuiService.cpp");
        Check(backend.find("CmdListsCount") == std::string::npos && overlay.find("CmdListsCount") == std::string::npos,
              "the overlay reads CmdLists.Size, the count ImGui still fills in");
        Check(overlay.find("cyber_engine_tweaks.asi") != std::string::npos,
              "the overlay will not start beside Cyber Engine Tweaks");
        Check(overlay.find("IniFilename = nullptr") != std::string::npos,
              "the overlay writes no imgui.ini into the game folder");
    }

    // ---------------------------------------------------------------- 6. SAVES KEEP THE CHARACTER
    // A world template other than the one that built a character rewrote its perks and skills,
    // and nothing recorded what it was wearing.
    {
        const auto save = ReadRepoFile("code/server/native/Systems/ChatSystem.cpp");
        const auto record = ReadRepoFile("code/server/native/CharacterRecord.h");
        const auto inventory = ReadRepoFile("code/assets/redscript/Inventory.reds");
        Check(save.find("level = std::max(level, kept->second)") != std::string::npos,
              "a save never lowers a skill");
        Check(save.find("!keepStoredLook && pPlayer->Puppet") != std::string::npos,
              "a possessions-only save does not blank the live look");
        Check(record.find("Attributes, Perks, Worn, Vehicles") != std::string::npos,
              "what a character wears survives a server restart");
        Check(inventory.find("MpInventory.PerksFellShort(network, player, development)") != std::string::npos,
              "a perk restore that fell short does not become the record");
        Check(inventory.find("MpInventory.RestoreWorn(network, player, transaction)") != std::string::npos,
              "the restore puts worn items back on");
    }

    // ---------------------------------------------------------------- 7. OTHER PLAYERS' WEAPONS
    // The server relayed these for months with nothing on the client reading them.
    {
        const auto client = ReadRepoFile("code/client/App/World/NetworkWorldSystem.cpp");
        const auto script = ReadRepoFile("code/assets/redscript/World/AppearanceSystem.reds");
        const auto server = ReadRepoFile("code/server/native/Game/Level.cpp");
        Check(client.find("RegisterHandler<&NetworkWorldSystem::HandleWeaponEvent>") != std::string::npos,
              "the client listens for other players' weapons");
        Check(client.find("\"SetRemoteWeapon\"") != std::string::npos && script.find("func SetRemoteWeapon") != std::string::npos,
              "the weapon event reaches the script that puts it in the hand");
        Check(server.find("SendHeldWeapon(pPlayerComponent->Connection, aEntity)") != std::string::npos,
              "a late joiner is told what is already in someone's hand");
    }

    std::printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
