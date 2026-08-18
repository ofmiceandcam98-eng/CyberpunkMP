using System.Security;
using CyberpunkSdk.Internal;
using EmbedIO;
using EmbedIO.Actions;
using EmbedIO.Authentication;
using Server.Loader.Extensions;
using Swan.Logging;

namespace Server.Loader.Systems
{
    internal class WebApiPluginDto
    {
        public required string Name { get; set; }
    }

    internal class WebApi
    {
        private Plugins Plugins { get; set; }

        private Statistics Statistics { get; }

        private Task? ServerTask { get; set; }

        private bool Running { get; set; } = true;

        internal WebApi(Plugins plugins, Statistics statistics)
        {
            Statistics = statistics;
            Plugins = plugins;

            var port = IConfig.Get().Port;
            var url = $"http://+:{port}";

            ServerTask = CreateWebServer(url).RunAsync();
        }

        ~WebApi()
        {
            ServerTask?.Dispose();
        }

        private WebServer CreateWebServer(string url)
        {
            Logger.UnregisterLogger<ConsoleLogger>();

            var server = new WebServer(o => o
                .WithUrlPrefix(url)
                .WithMode(HttpListenerMode.EmbedIO))
                .WithModule(new ActionModule("/api/v1/mods/", HttpVerbs.Get, HandleModsRoute))
                .WithModule(new ActionModule("/api/v1/statistics/", HttpVerbs.Get, HandleStatistics))
                .WithModule(new ActionModule("/api/v1/status/", HttpVerbs.Get, HandleStatus));

            RegisterAuthentication(server);
            // AFTER authentication on purpose: EmbedIO evaluates modules in registration
            // order, so anything registered before the auth module is public (that is how
            // /status stays open) and anything after it is challenged. Lifecycle control
            // must be on the challenged side.
            RegisterAdmin(server);
            RegisterPlugins(server);
            RegisterAssets(server);
            return server;
        }

        private void RegisterPlugins(WebServer server)
        {
            var withAssets = Plugins.GetPlugins(p => p.Assets != null);

            foreach (var plugin in withAssets)
            {
                server.WithStaticFolder(
                    baseRoute: plugin.GetAssetsUrl,
                    fileSystemPath: plugin.GetAssetsPath,
                    isImmutable: EnvironmentHelper.IsRelease,
                    configure: m => { m.AddCustomMimeType(".umd.js", "application/javascript"); });
            }
            var withHooks = Plugins.GetPlugins(p => p.WebApi != null);

            foreach (var plugin in withHooks)
            {
                var hook = plugin.WebApi!;
                var controller = hook.BuildController()();

                server.WithWebApi(
                    baseRoute: plugin.GetWebApiUrl,
                    configure: m => m.WithController(controller));
            }

            server.WithModule(new ActionModule("/api/v1/plugins/", HttpVerbs.Get, HandleListPlugins));
        }

        private void RegisterAssets(WebServer server)
        {
            var assetsPath = FileSystemHelper.GetPath("assets");

            server.WithStaticFolder(
                baseRoute: "/",
                fileSystemPath: assetsPath,
                isImmutable: EnvironmentHelper.IsRelease);
        }

        private void RegisterAuthentication(WebServer server)
        {
            if (EnvironmentHelper.IsDebug)
            {
                return;
            }

            // NOTE: should be replaced with Discord authentication later.
            var username = Environment.GetEnvironmentVariable("CYBERPUNKMP_ADMIN_USERNAME");
            var password = Environment.GetEnvironmentVariable("CYBERPUNKMP_ADMIN_PASSWORD");

            if (username == null || password == null)
            {
                throw new SecurityException("You must provide admin credentials using environment variables.");
            }

            // NOTE: always allow access to the following routes for the Server
            //       List System.
            var module = new AdminAuthModule([
                "/api/v1/mods",
                "/api/v1/statistics",
            ]);

            module.WithAccount(username, password);
            server.WithModule(module);
        }

        /// <summary>
        /// Remote lifecycle, for the launcher's admin panel. Exiting IS restarting - the
        /// container's restart policy brings the process straight back - and stop is a
        /// marker in the config volume that makes the next boot idle in stopped mode.
        /// </summary>
        private void RegisterAdmin(WebServer server)
        {
            server.WithModule(new ActionModule("/api/v1/admin/restart", HttpVerbs.Post, async context =>
            {
                await context.SendDataAsync(new { Ok = true, Action = "restarting" });
                Lifecycle.ExitSoon();
            }));

            server.WithModule(new ActionModule("/api/v1/admin/stop", HttpVerbs.Post, async context =>
            {
                Lifecycle.RequestStop();
                await context.SendDataAsync(new { Ok = true, Action = "stopping" });
                Lifecycle.ExitSoon();
            }));

            // Meaningful only in stopped mode (Lifecycle.RunStoppedMode serves it there);
            // answered here too so a start against a running server succeeds instead of 404ing.
            server.WithModule(new ActionModule("/api/v1/admin/start", HttpVerbs.Post,
                context => context.SendDataAsync(new { Ok = true, Action = "already-running" })));
        }

        #region Routes

        private Task HandleModsRoute(IHttpContext context)
        {
            return context.SendDataAsync(new
            {
                Mods = Plugins.ClientMods
            });
        }

        private Task HandleStatistics(IHttpContext context)
        {
            return context.SendDataAsync(new
            {
                Statistics.Rpcs
            });
        }

        /// <summary>
        /// Public server status, for the launcher.
        ///
        /// Deliberately unauthenticated and deliberately boring: whether the server is up
        /// and how busy it is. No player names, no ids, nothing about who is on - a launcher
        /// showing "12 players" is useful, a launcher listing who is currently online is a
        /// privacy problem and a griefing tool.
        ///
        /// Simply reaching this endpoint proves the server is up, so there is no "online"
        /// field - if you got a reply, it is online.
        /// </summary>
        private Task HandleStatus(IHttpContext context)
        {
            var players = 0;

            try
            {
                var system = CyberpunkSdk.Server.PlayerSystem;
                if (system?.PlayerIds != null)
                {
                    foreach (var _ in system.PlayerIds) players++;
                }
            }
            catch
            {
                // Status must never throw. A launcher that cannot read the count should show
                // "unknown", not fail to load - and an exception here would make the server
                // look down when it is fine.
            }

            return context.SendDataAsync(new
            {
                Players = players,
                Uptime = (int)(DateTime.UtcNow - StartedAtUtc).TotalSeconds,
                State = "running"
            });
        }

        private static readonly DateTime StartedAtUtc = DateTime.UtcNow;

        private Task HandleListPlugins(IHttpContext context)
        {
            var withHooks = Plugins.GetPlugins(p => p.WebApi != null);

            return context.SendDataAsync(
                withHooks.Select(plugin => new WebApiPluginDto
                    {
                        Name = plugin.Name,
                    })
                    .ToList()
            );
        }

        #endregion
    }

    internal class AdminAuthModule(string[] whitelist) : BasicAuthenticationModule("/", "Admin")
    {
        protected override Task<bool> VerifyCredentialsAsync(string path, string userName, string password,
            CancellationToken cancellationToken)
        {
            if (whitelist.Any(p => p == path))
            {
                return Task.FromResult(true);
            }

            return base.VerifyCredentialsAsync(path, userName, password, cancellationToken);
        }
    }
}