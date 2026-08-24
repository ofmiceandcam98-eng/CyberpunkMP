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
                .WithModule(new ActionModule("/api/v1/status/", HttpVerbs.Get, HandleStatus))
                .WithModule(new ActionModule("/api/v1/logs/", HttpVerbs.Post, HandleLogUpload));

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
            // Permanently an empty list: the CurseForge ClientMods lane is retired
            // (the helper rule, crew decree 2026-08-22 - see Plugins.DownloadMods).
            // The route itself stays so anything old that fetches it gets an honest
            // "no mods here" instead of a 404. Client mods ship via the launcher's
            // curated modlist and the signed manifest, nowhere else.
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
                State = "running",

                // Which environment this deploy runs, so the launcher can see client/server
                // skew BEFORE the game boots and the protocol gate kicks. Empty strings when
                // no manifest is deployed (migration) - absence of the fields would make old
                // launchers' field-presence server-age heuristic misfire.
                ManifestVersion,
                Release
            });
        }

        private static readonly DateTime StartedAtUtc = DateTime.UtcNow;

        /// <summary>
        /// Read once at startup from the same config/server-manifest.json the native
        /// server enforces at the door. This side only REPORTS it; enforcement lives in
        /// GameServer.cpp. A malformed or missing file reports empty, never throws -
        /// status must stay boring.
        /// </summary>
        private static readonly string ManifestVersion = ReadManifestField("manifestVersion");
        private static readonly string Release = ReadManifestField("release");

        private static string ReadManifestField(string field)
        {
            try
            {
                var path = Path.Combine(AppContext.BaseDirectory, "config", "server-manifest.json");
                if (!File.Exists(path))
                    return "";

                using var doc = System.Text.Json.JsonDocument.Parse(File.ReadAllText(path));
                return doc.RootElement.TryGetProperty(field, out var value) ? value.GetString() ?? "" : "";
            }
            catch
            {
                return "";
            }
        }

        private const int MaxLogUploadBytes = 4 * 1024 * 1024;
        private const int LogsKeptPerPlayer = 10;

        /// <summary>
        /// Client log intake, for the launcher.
        ///
        /// Launchers push each session's mod log here so debugging never depends on a
        /// player finding a file and pasting it somewhere. Filed under
        /// logs/clients/&lt;player&gt;/, newest ten kept per player - older uploads are
        /// deleted on arrival of new ones, so the folder is a rolling window, not an
        /// archive.
        ///
        /// Deliberately unauthenticated (players have no admin credentials), so it is
        /// bounded instead: 4 MB cap per upload, names reduced to a safe character set
        /// (no path traversal possible), and the per-player rotation caps disk use.
        /// </summary>
        private async Task HandleLogUpload(IHttpContext context)
        {
            if (context.Request.ContentLength64 > MaxLogUploadBytes)
            {
                context.Response.StatusCode = 413;
                await context.SendDataAsync(new { Ok = false, Error = "Log too large" });
                return;
            }

            var player = SanitizeName(context.Request.QueryString["player"], "unknown");
            var file = SanitizeName(context.Request.QueryString["file"],
                $"client-{DateTime.UtcNow:yyyy-MM-dd_HH-mm-ss}.log");

            if (!file.EndsWith(".log"))
            {
                file += ".log";
            }

            var body = await context.GetRequestBodyAsStringAsync();

            if (body.Length > MaxLogUploadBytes)
            {
                context.Response.StatusCode = 413;
                await context.SendDataAsync(new { Ok = false, Error = "Log too large" });
                return;
            }

            var dir = FileSystemHelper.GetPath("logs", "clients", player);
            Directory.CreateDirectory(dir);
            await File.WriteAllTextAsync(Path.Combine(dir, file), body);

            // The rolling window. Session logs are timestamped so re-uploads overwrite
            // in place rather than pile up; everything beyond the newest ten goes.
            var stale = Directory.GetFiles(dir, "*.log")
                .OrderByDescending(File.GetLastWriteTimeUtc)
                .Skip(LogsKeptPerPlayer);

            foreach (var old in stale)
            {
                try { File.Delete(old); } catch { /* a locked file just waits for next time */ }
            }

            await context.SendDataAsync(new { Ok = true, Stored = file });
        }

        /// <summary>
        /// Reduces untrusted input to a string safe to use as a file or folder name:
        /// letters, digits, dot, dash and underscore only, no leading/trailing dots
        /// (so ".." cannot survive), bounded length. Anything left empty becomes the
        /// fallback.
        /// </summary>
        private static string SanitizeName(string? value, string fallback)
        {
            if (string.IsNullOrWhiteSpace(value))
            {
                return fallback;
            }

            var cleaned = new string(value
                .Where(c => char.IsLetterOrDigit(c) || c is '.' or '_' or '-')
                .ToArray()).Trim('.');

            if (cleaned.Length == 0)
            {
                return fallback;
            }

            return cleaned.Length > 80 ? cleaned[..80] : cleaned;
        }

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