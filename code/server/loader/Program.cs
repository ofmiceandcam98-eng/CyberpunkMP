using System.Runtime.InteropServices;

internal class Program
{
  
    private static void SetupCallbacks()
    {
        CyberpunkMp.TUpdateCallback updateCallback = new CyberpunkMp.TUpdateCallback(Server.Loader.Server.Update);
        GCHandle.Alloc(updateCallback);
        CyberpunkMp.ServerAPI.SetUpdateCallback(updateCallback);

        CyberpunkMp.TPlayerEvent joinCallback = new CyberpunkMp.TPlayerEvent(Server.Loader.Server.PlayerJoin);
        GCHandle.Alloc(joinCallback);
        CyberpunkMp.ServerAPI.SetPlayerJoinCallback(joinCallback);

        CyberpunkMp.TPlayerEvent leaveCallback = new CyberpunkMp.TPlayerEvent(Server.Loader.Server.PlayerLeft);
        GCHandle.Alloc(leaveCallback);
        CyberpunkMp.ServerAPI.SetPlayerLeftCallback(leaveCallback);
    }

    // A stop signal only asks the tick loop to end. Main then falls through to Exit on its own
    // thread, so the final save runs with nothing else touching the stores.
    private static void StopRequested(PosixSignalContext context)
    {
        context.Cancel = true;
        CyberpunkMp.ServerAPI.RequestStop();
    }

    private static void Main(string[] args)
    {
        // An admin stopped this server. Idle in stopped mode - status plus an
        // authenticated start endpoint, nothing else - instead of booting the game.
        // See Systems/Lifecycle.cs for how the marker and the container's restart
        // policy fit together.
        if (Server.Loader.Systems.Lifecycle.IsStopRequested)
        {
            Server.Loader.Systems.Lifecycle.RunStoppedMode();
            return;
        }

        // docker stop sends SIGTERM. Left alone, the runtime answers it by exiting on its own
        // thread: that ran ServerAPI::Exit and destroyed GameServer while this thread was still
        // ticking inside it, so the final save raced live play and every stop ended in
        // free(): invalid pointer. atlas:both-game-servers-abort-with-free-invalid-pointer-on-every-s
        using var sigterm = PosixSignalRegistration.Create(PosixSignal.SIGTERM, StopRequested);
        using var sigint = PosixSignalRegistration.Create(PosixSignal.SIGINT, StopRequested);

        try
        {
            if (CyberpunkMp.ServerAPI.Initialize())
            {
                SetupCallbacks();

                CyberpunkMp.ServerAPI.Run();
            }
        }
        catch (Exception e)
        {
            Console.WriteLine(e);
        }
        finally
        {
            CyberpunkMp.ServerAPI.Exit();
        }
    }
}