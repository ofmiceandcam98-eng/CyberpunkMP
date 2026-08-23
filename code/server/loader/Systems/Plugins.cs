using System.IO.Compression;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using CurseForge.APIClient;
using CurseForge.APIClient.Models.Enums;
using CurseForge.APIClient.Models.Files;
using CurseForge.APIClient.Models.Mods;
using CyberpunkSdk;
using CyberpunkSdk.Systems;
using File = System.IO.File;


namespace Server.Loader.Systems
{
    internal class Artifact
    {
        public string Checksum { get; set; } = "";
        public List<string> Directories { get; set; } = new List<string>();
    }

    internal class Cache
    {
        public Dictionary<int, Artifact> Mods { get; set; } = new Dictionary<int, Artifact>();
    }

    internal class Configuration
    {
        public List<int>? ServerMods { get; set; } = new List<int>();
        public List<int>? ClientMods { get; set; } = new List<int>();
        public string ApiKey { get; set; } = "";
    }

    internal class ModDef
    {
        public string Name { get; set; } = "";
        public int Id { get; set; }
        public string Description { get; set; } = "";
        public string Url { get; set; } = "";
        public string Checksum { get; set; } = "";
    }

    internal class PluginInfo
    {
        public string Name { get; set; } = "";
        public string FullName { get; set; } = "";
        public IWebApiHook? WebApi { get; set; }
        public string? Assets { get; set; }

        public string GetWebApiUrl => $"/api/v1/plugins/{Name.ToLower()}";
        
        public string GetAssetsUrl => $"/api/v1/plugins/{Name.ToLower()}/assets/";
        public string GetAssetsPath => Assets!;
    }

    internal class Plugins
    {
        private Configuration configuration = new();
        private Cache cache = new();
        private List<ModDef> serverMods = [];
        private List<ModDef> clientMods = [];
        private List<PluginInfo> plugins = [];
        private Logger logger = new("SDK");

        // Kept so a plugin can be unloaded and loaded again without restarting.
        private readonly Dictionary<string, PluginLoadContext> contexts = new();
        private RpcManager rpcManager = null!;
        private string pluginsDirectory = "";

        // Hot reload. The watcher fires on a background thread, so it only records
        // what changed; the reload itself runs from the server update loop, where it
        // cannot race with an RPC dispatch.
        private FileSystemWatcher? watcher;
        private readonly Dictionary<string, DateTime> pendingReloads = new();
        private readonly object pendingLock = new();

        // A build writes the dll several times in quick succession. Wait for it to go
        // quiet before reloading, otherwise we load a half-written file.
        private static readonly TimeSpan ReloadQuietPeriod = TimeSpan.FromMilliseconds(750);

        public IList<ModDef> ClientMods => clientMods;
        
        public IList<PluginInfo> GetPlugins(Func<PluginInfo, bool> predicate) => plugins.Where(predicate).ToList();

        private void LoadConfiguration(string path)
        {
            try
            {
                Directory.CreateDirectory(Path.Combine(path, "config"));
                var filepath = Path.Combine(path, "config", "mods.json");
                if (Path.Exists(filepath))
                {
                    var content = File.ReadAllText(filepath);
                    Configuration? config = JsonSerializer.Deserialize<Configuration>(content);

                    if (config != null)
                    {
                        configuration = config;
                    }
                }
                else
                {
                    JsonSerializerOptions options = new()
                    {
                        WriteIndented = true
                    };

                    var content = JsonSerializer.Serialize(configuration, options);
                    File.WriteAllText(filepath, content);
                }

                filepath = Path.Combine(path, "config", "_cache.json");
                if (Path.Exists(filepath))
                {
                    var content = File.ReadAllText(filepath);
                    Cache? c = JsonSerializer.Deserialize<Cache>(content);

                    if (c != null)
                    {
                        cache = c;
                    }
                }
            }
            catch (Exception ex)
            {
                logger.Error(ex.ToString());
            }
        }

        private void GetMods(List<int>? mods, bool client)
        {
            if (mods == null || mods.Count == 0)
                return;

            using (var cfApiClient = new ApiClient(configuration.ApiKey))
            {
                var modList = cfApiClient.GetModsByIdListAsync(new GetModsByIdsListRequestBody
                {
                    ModIds = mods
                }).Result;

                foreach (var m in modList.Data)
                {
                    ModDef def = new();

                    def.Description = m.Summary;
                    def.Id = m.Id;
                    def.Name = m.Name;
                    foreach (var f in m.LatestFiles)
                    {
                        if (f.Id != m.MainFileId)
                            continue;

                        if (f.FileStatus == FileStatus.Approved)
                        {
                            def.Url = f.DownloadUrl;
                            foreach (var hash in f.Hashes)
                            {
                                if (hash.Algo == HashAlgo.Sha1)
                                {
                                    def.Checksum = hash.Value;
                                    break;
                                }
                            }
                        }
                    }

                    if (client)
                        clientMods.Add(def);
                    else
                        serverMods.Add(def);
                }
            }
        }

        private void DownloadMods(string root)
        {
            logger.Info("Checking mods...");

            try
            {
                // ClientMods is deliberately NOT resolved anymore (the helper rule,
                // crew decree 2026-08-22: mods are helpers, never variables). This
                // lane advertised operator-picked CurseForge downloads to clients -
                // a distribution channel with no curation, no dependency ordering,
                // no ownership guard, and a boot-time dependency on the CurseForge
                // API. Client mods are the launcher's job: the curated modlist and
                // the signed manifest. The config field stays parseable so old
                // mods.json files load; its ids are ignored and /api/v1/mods serves
                // an empty list. ServerMods (server-side plugins) are unaffected.
                if (configuration.ClientMods is { Count: > 0 })
                    logger.Info($"Ignoring {configuration.ClientMods.Count} ClientMods entr(ies) in mods.json - client mods ship via the launcher's curated list, not this lane.");

                GetMods(configuration.ServerMods, false);

                DownloadServerMods(root);
            }
            catch (Exception ex)
            {
                logger.Error(ex.ToString());
            }
        }

        public static async Task DownloadFileAsync(string url, string downloadPath)
        {
            HttpClient client = new HttpClient();
            try
            {
                HttpResponseMessage response = await client.GetAsync(url);
                response.EnsureSuccessStatusCode();

                byte[] fileBytes = await response.Content.ReadAsByteArrayAsync();
                await File.WriteAllBytesAsync(downloadPath, fileBytes);

                Console.WriteLine("File downloaded successfully.");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"An error occurred: {ex.Message}");
            }
        }

        private void DownloadMod(ModDef def)
        {
            if (!cache.Mods.TryGetValue(def.Id, out var artifact))
            {
                // If the artifact does not exist in the cache, create a new one
                artifact = new Artifact { Checksum = "", Directories = new List<string>() };
                cache.Mods[def.Id] = artifact;
            }

            if (def.Checksum == artifact.Checksum)
            {
                logger.Info($"Mod {def.Id} - {def.Name} up-to-date, using cache.");
                return;
            }

            logger.Info($"Mod {def.Id} - {def.Name} missing.");

            foreach (var directory in artifact.Directories)
            {
                logger.Debug($"Deleting {directory}");
                Directory.Delete(directory, true);
            }

            // Define the path for the downloaded zip file and extraction directory
            string downloadPath = Path.Combine("plugins", $"{def.Id}.zip");
            string tempExtractPath = Path.Combine("plugins", $"{def.Id}_temp");

            // Ensure the temporary extraction directory exists
            if (Directory.Exists(tempExtractPath))
            {
                Directory.Delete(tempExtractPath, true);
            }

            Directory.CreateDirectory(tempExtractPath);

            // Download the zip file
            DownloadFileAsync(def.Url, downloadPath).GetAwaiter().GetResult();

            // Unzip the file to the temporary extraction path
            ZipFile.ExtractToDirectory(downloadPath, tempExtractPath);

            // Clean up the downloaded zip file
            File.Delete(downloadPath);

            // Move contents of the temporary extraction directory to the plugins directory
            var topLevelDirectories = Directory.GetDirectories(tempExtractPath);
            foreach (var directory in topLevelDirectories)
            {
                string destinationPath = Path.Combine("plugins", Path.GetFileName(directory));
                Directory.Move(directory, destinationPath);
            }

            // Clean up the temporary extraction directory
            Directory.Delete(tempExtractPath, true);

            // Update the artifact directories to point to the new locations
            artifact.Directories.Clear();
            artifact.Directories.AddRange(topLevelDirectories.Select(dir =>
                Path.Combine("plugins", Path.GetFileName(dir))));

            // Update the artifact checksum
            artifact.Checksum = def.Checksum;

            logger.Info($"Mod {def.Id} - {def.Name} downloaded and extracted successfully.");
        }

        private void DownloadServerMods(string root)
        {
            foreach (var def in serverMods)
            {
                DownloadMod(def);
            }

            var filepath = Path.Combine(root, "config", "_cache.json");
            JsonSerializerOptions options = new()
            {
                WriteIndented = true
            };

            var content = JsonSerializer.Serialize(cache, options);
            File.WriteAllText(filepath, content);
        }

        private IWebApiHook? DetectWebApiHook(Type plugin, string name)
        {
            var property = plugin.GetProperty("Instance", BindingFlags.Static | BindingFlags.Public);
            if (property == null)
            {
                return null;
            }

            var instance = property.GetValue(null);
            if (instance is not IWebApiHook hook)
            {
                return null;
            }
            return hook;
        }

        private string? DetectAssets(string path, string name)
        {
            path = Path.Combine(path, "assets");

            if (!Directory.Exists(path))
            {
                return null;
            }

            if (Directory.GetFiles(path).Length == 0)
            {
                return null;
            }
            return path;
        }

        internal Plugins(RpcManager rpcManager)
        {
            // Get the location of the current assembly and its containing directory
            string currentAssemblyLocation = Assembly.GetExecutingAssembly().Location;
            string baseDirectory = Path.GetDirectoryName(currentAssemblyLocation)!;
            string exeRoot = baseDirectory;

            this.rpcManager = rpcManager;
            this.pluginsDirectory = Path.Combine(baseDirectory, "plugins");

            LoadConfiguration(exeRoot);
            DownloadMods(exeRoot);

            // Get all subdirectories in the base directory
            string[] subDirectories = Directory.GetDirectories(pluginsDirectory);

            foreach (string directory in subDirectories)
            {
                LoadPlugin(directory);
            }

            StartWatching();
        }

        /// <summary>
        /// Watch the plugins directory and reload a plugin when its dll is rebuilt.
        /// </summary>
        private void StartWatching()
        {
            try
            {
                watcher = new FileSystemWatcher(pluginsDirectory)
                {
                    Filter = "*.dll",
                    IncludeSubdirectories = true,
                    NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size
                };

                void OnChanged(object _, FileSystemEventArgs e) => QueueReload(e.FullPath);

                watcher.Changed += OnChanged;
                watcher.Created += OnChanged;
                watcher.Renamed += (_, e) => QueueReload(e.FullPath);

                watcher.EnableRaisingEvents = true;

                logger.Info($"Watching {pluginsDirectory} - rebuild a plugin and it reloads automatically");
            }
            catch (Exception ex)
            {
                logger.Warn($"Could not watch the plugins directory, hot reload disabled: {ex.Message}");
            }
        }

        private void QueueReload(string changedPath)
        {
            // Only care about a plugin's own assembly: plugins/EmoteSystem/EmoteSystem.dll
            var directory = Path.GetDirectoryName(changedPath);
            if (directory == null)
                return;

            var pluginName = new DirectoryInfo(directory).Name;

            if (!string.Equals(Path.GetFileNameWithoutExtension(changedPath), pluginName,
                               StringComparison.OrdinalIgnoreCase))
                return;

            lock (pendingLock)
            {
                pendingReloads[pluginName] = DateTime.UtcNow;
            }
        }

        /// <summary>
        /// Called from the server update loop. Reloads plugins whose files have
        /// stopped changing.
        /// </summary>
        public void ProcessPendingReloads()
        {
            if (watcher == null)
                return;

            List<string>? ready = null;

            lock (pendingLock)
            {
                if (pendingReloads.Count == 0)
                    return;

                var now = DateTime.UtcNow;
                foreach (var (name, changedAt) in pendingReloads)
                {
                    if (now - changedAt < ReloadQuietPeriod)
                        continue;

                    (ready ??= new List<string>()).Add(name);
                }

                if (ready != null)
                {
                    foreach (var name in ready)
                        pendingReloads.Remove(name);
                }
            }

            if (ready == null)
                return;

            foreach (var name in ready)
            {
                try
                {
                    Reload(name);
                }
                catch (Exception ex)
                {
                    logger.Error($"Reload of '{name}' failed: {ex.Message}");
                }
            }
        }

        /// <summary>
        /// Reload a single plugin by name (the directory name, e.g. "EmoteSystem").
        /// Returns true if it was loaded again successfully.
        ///
        /// RPC registration is idempotent on the native side, so a reloaded plugin
        /// re-registers the same (Klass, Function) pairs and gets the same ids back.
        /// That means already-connected clients keep working - they were sent those
        /// ids at authentication and never learn anything changed.
        ///
        /// Known limitation: WebApi routes are registered when the web server is
        /// built, so a plugin's WebApi hook is NOT re-registered by a reload. Restart
        /// the server if you changed one.
        /// </summary>
        public bool Reload(string pluginName)
        {
            var directory = Path.Combine(pluginsDirectory, pluginName);

            if (!Directory.Exists(directory))
            {
                logger.Warn($"Cannot reload '{pluginName}': no such plugin directory");
                return false;
            }

            // Drop the old plugin's entry and release its load context. The unload is
            // cooperative: the assembly goes away once the GC sees no live references,
            // which is why nothing may hold on to plugin types across a reload.
            plugins.RemoveAll(p => p.FullName == pluginName);

            if (contexts.TryGetValue(pluginName, out var oldContext))
            {
                contexts.Remove(pluginName);
                oldContext.Unload();

                GC.Collect();
                GC.WaitForPendingFinalizers();
            }

            logger.Info($"Reloading plugin: {pluginName}");
            return LoadPlugin(directory);
        }

        private bool LoadPlugin(string directory)
        {
            {
                string directoryName = new DirectoryInfo(directory).Name;
                string assemblyPath = Path.Combine(directory, directoryName + ".dll");

                // Check if the assembly file exists
                if (File.Exists(assemblyPath))
                {
                    try
                    {
                        // Load into a collectible context, from a copy in memory, so the
                        // file on disk is never locked and can be rebuilt in place.
                        var context = new PluginLoadContext(assemblyPath);
                        var assembly = context.LoadFromFileCopy(assemblyPath);
                        contexts[directoryName] = context;
                        var typeName = directoryName + ".Plugin";

                        var plugin = assembly.GetType(typeName);

                        if (plugin != null)
                        {
                            RuntimeHelpers.RunClassConstructor(plugin.TypeHandle);

                            rpcManager.ParseAssembly(assembly);
                            
                            var info = new PluginInfo();
                            info.Name = directoryName[..^"System".Length];
                            info.FullName = directoryName;
                            info.WebApi = DetectWebApiHook(plugin, info.Name);
                            info.Assets = DetectAssets(directory, info.Name.ToLower());
                            plugins.Add(info);
                            
                            var hasHook = info.WebApi != null;
                            var hasAssets = info.Assets != null;
                            logger.Info($"Loaded Plugin" +
                                        $"{(hasHook ? " + WebApi" : "")}" +
                                        $"{(hasAssets ? " + Assets" : "")}: {directoryName}");

                            return true;
                        }
                        else
                        {
                            logger.Warn($"Failed to load assembly: {assemblyPath}. Error: Missing type {typeName}");
                        }
                    }
                    catch (Exception ex)
                    {
                        // Handle exceptions, e.g., if the file is not a .NET assembly
                        logger.Warn($"Failed to load assembly: {assemblyPath}. Error: {ex.Message}");
                    }
                }
                else
                {
                    logger.Warn($"Expected assembly not found: {assemblyPath}");
                }
            }

            return false;
        }
    }
}