#include "ServerAPI.h"

#include "GameServer.h"

#include "WorldScriptInstance.h"
#include <PlayerManager.h>

#include <cstdio>

#ifndef _WIN32
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

static UniquePtr<GameServer> s_server;

#ifndef _WIN32
/*
 * Prints where an abort came from, then hands the signal on to whoever had it before.
 *
 * Every stop of both servers ends in free(): invalid pointer, and nothing names the caller.
 * The [shutdown] stage lines say WHEN it happens; this says which library freed the bad
 * pointer. SIGABRT only - the runtime owns SIGSEGV for managed null references, so a handler
 * there would print a trace for every one. Chained, so the process still dies exactly as it
 * did. atlas:both-game-servers-abort-with-free-invalid-pointer-on-every-s
 */
static struct sigaction s_previousAbort{};

static void PrintAbortTrace(int aSignal, siginfo_t* apInfo, void* apContext)
{
    static constexpr char kHeader[] = "[shutdown] SIGABRT - native backtrace:\n";
    write(STDERR_FILENO, kHeader, sizeof(kHeader) - 1);

    void* frames[64];
    backtrace_symbols_fd(frames, backtrace(frames, 64), STDERR_FILENO);

    if (s_previousAbort.sa_flags & SA_SIGINFO)
    {
        if (s_previousAbort.sa_sigaction)
            s_previousAbort.sa_sigaction(aSignal, apInfo, apContext);
    }
    else if (s_previousAbort.sa_handler != SIG_DFL && s_previousAbort.sa_handler != SIG_IGN)
    {
        s_previousAbort.sa_handler(aSignal);
    }
    // Default: returning lets abort() restore SIG_DFL and raise again, as it always did.
}

static void InstallAbortTrace()
{
    // backtrace() loads libgcc on its first call, which allocates. Do that now while malloc
    // is healthy, not inside a handler that runs because it is not.
    void* warm[1];
    backtrace(warm, 1);

    struct sigaction action{};
    action.sa_sigaction = &PrintAbortTrace;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGABRT, &action, &s_previousAbort);
}
#endif

bool ServerAPI::Initialize()
{
#ifndef _WIN32
    InstallAbortTrace();
#endif

    s_server = MakeUnique<GameServer>();

    std::atexit(&ServerAPI::Exit);

    return s_server->IsListening();
}

void ServerAPI::Run()
{
    s_server->Run();
}

void ServerAPI::RequestStop()
{
    GameServer::RequestStop();
}

void ServerAPI::Exit()
{
    // Straight to stderr, which is unbuffered, rather than through spdlog: these lines trace
    // the teardown that destroys spdlog's own statics.
    //
    // Runs twice on a clean stop - from Main's finally, then from atexit once the runtime
    // has shut down - so the second line is the marker that teardown got that far.
    if (!s_server)
    {
        std::fprintf(stderr, "[shutdown] ServerAPI::Exit: nothing to destroy (process exit)\n");
        return;
    }

    std::fprintf(stderr, "[shutdown] ServerAPI::Exit: destroying GameServer\n");
    s_server = nullptr;
    std::fprintf(stderr, "[shutdown] ServerAPI::Exit: GameServer destroyed\n");
}

TP_EXPORT void ServerAPI::SetUpdateCallback(TUpdateCallback apCallback)
{
    GServer->GetWorld()->GetScriptInstance()->SetUpdateCallback(apCallback);
}

void ServerAPI::SetPlayerJoinCallback(TPlayerEvent callback)
{
    GServer->GetWorld()->get_mut<PlayerManager>()->GetScriptInstance()->SetPlayerJoinCallback(callback);
}

void ServerAPI::SetPlayerLeftCallback(TPlayerEvent callback)
{
    GServer->GetWorld()->get_mut<PlayerManager>()->GetScriptInstance()->SetPlayerLeftCallback(callback);
}
