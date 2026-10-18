using Minecraft.Server.FourKit.Plugin;
using System.Reflection;
using System.Runtime.Loader;

namespace Minecraft.Server.FourKit;

internal sealed class PluginLoadContext : AssemblyLoadContext
{
    private readonly AssemblyDependencyResolver _resolver;
    private readonly string _pluginDirectory;

    public PluginLoadContext(string pluginPath)
        : base(isCollectible: false)
    {
        _pluginDirectory = Path.GetDirectoryName(Path.GetFullPath(pluginPath))!;
        _resolver = new AssemblyDependencyResolver(pluginPath);
    }

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        if (assemblyName.Name == typeof(ServerPlugin).Assembly.GetName().Name)
            return typeof(ServerPlugin).Assembly;

        string? path = _resolver.ResolveAssemblyToPath(assemblyName);
        if (path != null)
            return LoadFromAssemblyPath(path);

        if (assemblyName.Name != null)
        {
            string fallback = Path.Combine(_pluginDirectory, assemblyName.Name + ".dll");
            if (File.Exists(fallback))
                return LoadFromAssemblyPath(fallback);
        }

        return null;
    }

    protected override IntPtr LoadUnmanagedDll(string unmanagedDllName)
    {
        string? path = _resolver.ResolveUnmanagedDllToPath(unmanagedDllName);
        if (path != null)
            return LoadUnmanagedDllFromPath(path);

        return IntPtr.Zero;
    }
}
