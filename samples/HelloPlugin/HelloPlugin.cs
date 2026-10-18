using Minecraft.Server.FourKit;
using Minecraft.Server.FourKit.Event;
using Minecraft.Server.FourKit.Event.Player;
using Minecraft.Server.FourKit.Plugin;

namespace HelloPlugin;

/// <summary>
/// Minimal sample plugin used to verify the FourKit plugin loader and event
/// dispatcher are wired up correctly. Greets each player on join.
/// </summary>
public class HelloPlugin : ServerPlugin
{
    public override string name => "HelloPlugin";
    public override string version => "1.0.0";
    public override string author => "LCE-Revelations";

    public override void onEnable()
    {
        Console.WriteLine($"[HelloPlugin] {name} v{version} enabled.");
        FourKit.addListener(new HelloListener());
    }

    public override void onDisable()
    {
        Console.WriteLine($"[HelloPlugin] {name} v{version} disabled.");
    }
}

internal sealed class HelloListener : Listener
{
    [EventHandler(Priority = EventPriority.Normal)]
    public void onPlayerJoin(PlayerJoinEvent e)
    {
        var playerName = e.getPlayer().getName();
        Console.WriteLine($"[HelloPlugin] {playerName} joined.");
        e.setJoinMessage($"Welcome, {playerName}!");
    }
}
