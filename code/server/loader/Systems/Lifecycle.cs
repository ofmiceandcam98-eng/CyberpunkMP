using System.Security;
using EmbedIO;
using EmbedIO.Actions;
using EmbedIO.Authentication;
using Swan.Logging;

namespace Server.Loader.Systems
{
    /// <summary>
    /// Remote lifecycle for a containerized server.
    ///
    /// The container's restart policy is the actual muscle here: exiting the process IS
    /// a restart, because docker brings the container straight back. "Stop" therefore
    /// means writing a marker into the config volume and exiting - the process comes
    /// back, sees the marker, and idles in stopped mode (status + admin endpoints only,
    /// no game server) until an admin's start request clears the marker and exits again.
    ///
    /// The marker lives in the config volume so it survives container recreation: a
    /// server an admin stopped STAYS stopped through host reboots and redeploys, until
    /// someone deliberately starts it.
    /// </summary>
    public static class Lifecycle
    {
        public static string MarkerPath => FileSystemHelper.GetPath("config", "server-stopped.marker");

        public static bool IsStopRequested => File.Exists(MarkerPath);

        public static void RequestStop()
        {
            Directory.CreateDirectory(Path.GetDirectoryName(MarkerPath)!);
            File.WriteAllText(MarkerPath, $"stopped by an admin at {DateTime.UtcNow:O}\n");
        }

        public static void ClearStop()
        {
            if (File.Exists(MarkerPath))
                File.Delete(MarkerPath);
        }

        /// <summary>
        /// Exit AFTER the HTTP response has had a moment to flush - killing the process
        /// inside the handler races the reply, and the admin's launcher would report a
        /// network error for an action that actually succeeded.
        /// </summary>
        public static void ExitSoon()
        {
            Task.Run(async () =>
            {
                await Task.Delay(500);
                Environment.Exit(0);
            });
        }

        /// <summary>
        /// The whole server while stopped: answers status honestly, accepts an
        /// authenticated start, runs nothing else. Blocks forever - leaving this state
        /// is always a process exit.
        /// </summary>
        public static void RunStoppedMode()
        {
            try { Logger.UnregisterLogger<ConsoleLogger>(); } catch { /* only registered when a console exists */ }

            var port = CyberpunkSdk.Internal.IConfig.Get().Port;
            var startedAt = DateTime.UtcNow;

            var server = new WebServer(o => o
                    .WithUrlPrefix($"http://+:{port}")
                    .WithMode(HttpListenerMode.EmbedIO))
                .WithModule(new ActionModule("/api/v1/status/", HttpVerbs.Get, ctx => ctx.SendDataAsync(new
                {
                    Players = 0,
                    Uptime = (int)(DateTime.UtcNow - startedAt).TotalSeconds,
                    State = "stopped"
                })));

            if (!EnvironmentHelper.IsDebug)
            {
                var username = Environment.GetEnvironmentVariable("CYBERPUNKMP_ADMIN_USERNAME");
                var password = Environment.GetEnvironmentVariable("CYBERPUNKMP_ADMIN_PASSWORD");

                if (username == null || password == null)
                    throw new SecurityException("You must provide admin credentials using environment variables.");

                var auth = new AdminAuthModule(["/api/v1/status"]);
                auth.WithAccount(username, password);
                server.WithModule(auth);
            }

            server.WithModule(new ActionModule("/api/v1/admin/start", HttpVerbs.Post, async ctx =>
            {
                ClearStop();
                await ctx.SendDataAsync(new { Ok = true, Action = "starting" });
                ExitSoon(); // the container restarts us without the marker = a real boot
            }));

            Console.WriteLine($"Server is STOPPED (an admin stopped it). Listening on :{port} for a start request.");

            server.RunAsync().Wait();
        }
    }
}
