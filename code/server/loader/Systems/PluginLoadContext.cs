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
            // CyberpunkSdk must resolve to the host's copy, otherwise the plugin gets
            // its own duplicate types and nothing it registers matches the server's.
            if (assemblyName.Name == "CyberpunkSdk")
                return null;

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
