using System.Reflection;
using System.Runtime.Loader;

namespace Server.Loader.Systems
{
    /// <summary>
    /// A collectible load context for a single plugin, so its assembly can be
    /// unloaded and a rebuilt copy loaded in its place without restarting the server.
    ///
    /// Assembly.LoadFrom puts assemblies in the default context, which can never be
    /// unloaded - the file also stays locked on Windows, so a rebuild can't overwrite
    /// it while the server runs.
    ///
    /// Plugin assemblies are read into memory rather than loaded from the path, so
    /// the DLL on disk is never locked and can be replaced at any time.
    /// </summary>
    internal class PluginLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver resolver;

        public string PluginPath { get; }

        public PluginLoadContext(string pluginPath)
            : base(name: $"Plugin:{Path.GetFileNameWithoutExtension(pluginPath)}", isCollectible: true)
        {
            PluginPath = pluginPath;
            resolver = new AssemblyDependencyResolver(pluginPath);
        }

        /// <summary>
        /// Load an assembly without locking the file on disk.
        /// </summary>
        public Assembly LoadFromFileCopy(string path)
        {
            var bytes = File.ReadAllBytes(path);

            // Load the pdb alongside it when present, so exceptions still carry line
            // numbers - the main reason to iterate on a plugin in the first place.
            var pdbPath = Path.ChangeExtension(path, ".pdb");
            if (File.Exists(pdbPath))
            {
                var pdb = File.ReadAllBytes(pdbPath);
                using var assemblyStream = new MemoryStream(bytes);
                using var pdbStream = new MemoryStream(pdb);
                return LoadFromStream(assemblyStream, pdbStream);
            }

            using var stream = new MemoryStream(bytes);
            return LoadFromStream(stream);
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name is null)
                return null;

            // Anything the host has already loaded must be SHARED, never duplicated.
            //
            // Two copies of an assembly mean two sets of types with the same names that the
            // runtime treats as unrelated. That breaks silently rather than loudly: when the
            // plugin got its own EmbedIO, its [Route] attributes were a different type from
            // the ones the host's WebApiModule scans for, so a perfectly good controller
            // reported "contains no controller methods" and the server died on startup.
            //
            // Returning null defers to the default context. Only assemblies genuinely
            // private to the plugin get loaded here - which is also the only thing that
            // could be unloaded on reload anyway.
            foreach (var loaded in Default.Assemblies)
            {
                if (string.Equals(loaded.GetName().Name, assemblyName.Name, StringComparison.OrdinalIgnoreCase))
                    return null;
            }

            var path = resolver.ResolveAssemblyToPath(assemblyName);
            return path != null ? LoadFromAssemblyPath(path) : null;
        }

        protected override IntPtr LoadUnmanagedDll(string unmanagedDllName)
        {
            var path = resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
            return path != null ? LoadUnmanagedDllFromPath(path) : IntPtr.Zero;
        }
    }
}
