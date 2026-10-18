# FourKit Event Parity

Records which FourKit events ship in this build, mapped to the native hook sites that fire them. This document is the canonical reference for plugin authors: if an event is listed here, you can subscribe to it. Use the [donor reference](https://github.com/) for the upstream FourKit event catalog if you need a richer description of any specific event.

The Phase 1 reconnaissance showed that every donor hook-bearing source file in this repo was byte-identical to vanilla, so the donor's full set of hooks was applied to LCE-Revelations without policy-driven deferrals. Status is **PORTED** for everything below.

## Server lifecycle

| Event | Native hook site | Status |
|---|---|---|
| `FourKitBridge::Initialize` | `Minecraft.Server/Windows64/ServerMain.cpp` post-startup | PORTED |
| `FourKitBridge::Shutdown`   | `Minecraft.Server/Windows64/ServerMain.cpp` early-shutdown | PORTED |
| `FireWorldSave`             | `ServerMain.cpp` autosave trigger | PORTED |

## Player lifecycle

| Event | Native hook site | Status |
|---|---|---|
| `FirePlayerPreLogin`        | `Minecraft.Client/PendingConnection.cpp::handlePreLogin` | PORTED |
| `FirePlayerLogin` (offline) | `Minecraft.Client/PendingConnection.cpp::handleLogin` | PORTED |
| `FirePlayerLogin` (online)  | `Minecraft.Client/PendingConnection.cpp::handleAcceptedLogin` | PORTED |
| `FirePlayerJoin`            | `Minecraft.Client/PlayerConnection.cpp::tick` (first-tick gated by `hasDoneFirstTickFourKit`) | PORTED |
| `FirePlayerKick`            | `PlayerConnection.cpp::disconnect` | PORTED |
| `FirePlayerQuit`            | `PlayerConnection.cpp::disconnect` and `onDisconnect` paths | PORTED |
| `UpdatePlayerEntityId`      | Two `respawn(...)` sites in `PlayerConnection.cpp` | PORTED |
| `FirePlayerMove`            | `PlayerConnection.cpp::handleMovePlayer` | PORTED |

## Player gameplay

| Event | Native hook site | Status |
|---|---|---|
| `FirePlayerChat`               | `PlayerConnection.cpp::handleChat` | PORTED |
| `FireCommandPreprocess`        | `PlayerConnection.cpp::handleCommand` | PORTED |
| `HandlePlayerCommand`          | `PlayerConnection.cpp::handleCommand` | PORTED |
| `FirePlayerInteract` (USE_ITEM)            | `handleUseItem` (face == 255) | PORTED |
| `FirePlayerInteract` (RIGHT_CLICK_BLOCK)   | `handleUseItem` (face != 255) | PORTED |
| `FireBlockPlace`                            | `handleUseItem` post `useItemOn` | PORTED |
| `FirePlayerInteract` (LEFT_CLICK_BLOCK)    | `handlePlayerAction::START_DESTROY_BLOCK` | PORTED |
| `FirePlayerInteract` (LEFT_CLICK_AIR)      | `handleAnimate::SWING` | PORTED |
| `FirePlayerInteractEntity`                  | `handleInteract::INTERACT` (uses `MapEntityType`) | PORTED |
| `FirePlayerDropItem`                        | `handlePlayerAction::DROP_ITEM` and `DROP_ALL_ITEMS` | PORTED |

## Inventory

| Event | Native hook site | Status |
|---|---|---|
| `FireInventoryClick`         | `PlayerConnection.cpp::handleContainerClick` | PORTED |
| `FireSignChange`             | `PlayerConnection.cpp::handleSignUpdate` | PORTED |

## Block / world

The Phase 1 recon enumerated 17 hook-bearing files in `Minecraft.World/`. Each file was bulk-copied from the donor (all were vanilla in target). The hooks they contain cover:

| Source file | Hook category | Status |
|---|---|---|
| `AbstractContainerMenu.cpp` | Inventory open / interact | PORTED |
| `LivingEntity.cpp` | Entity damage, death | PORTED |
| `ItemEntity.cpp` | Item entity events | PORTED |
| `ThrownEnderpearl.cpp` | Teleport / portal | PORTED |
| `PistonBaseTile.cpp` | Block piston extend / retract | PORTED |
| `LiquidTileDynamic.cpp` | Block from-to (water / lava flow) | PORTED |
| `FireTile.cpp` | Block burn | PORTED |
| `GrassTile.cpp`, `Mushroom.cpp`, `Sapling.cpp` | Block spread / form | PORTED |
| `CactusTile.cpp`, `CocoaTile.cpp`, `CropTile.cpp`, `EggTile.cpp`, `NetherWartTile.cpp`, `ReedTile.cpp`, `StemTile.cpp` | Block grow | PORTED |

The exact `Fire*` calls inside each file match the donor verbatim.

## Console / commands

| Event | Native hook site | Status |
|---|---|---|
| `HandleConsoleCommand`        | `Minecraft.Server/Console/ServerCliEngine.cpp` (unknown-command branch) | PORTED |
| `GetPluginCommandHelp`        | `ServerCliEngine.cpp` (suggest-names branch) | PORTED |

## Native callback surface (C# → C++)

`FourKitNatives.cpp` was copied verbatim from the donor. All ~80 native callbacks (player damage / health / teleport / kick / ban, world tile get / set, inventory access, virtual containers, sound, explosion, etc.) are present. Phase 1 spot-checks confirmed signature compatibility against the engine APIs in this repo.

## Known gaps

None at the time of writing. If runtime testing in Phase 8 reveals an event that compiles but does not fire (e.g., the donor refactored the calling function in a way the recon missed), it will be added here as a deferred item.

## Out of scope (per the locked plan)

- Hot-reload (PluginLoadContext is non-collectible by design)
- Permission system
- Async / thread-safety hardening of the dispatcher
- New events beyond donor parity
- Managed unit tests for the API surface
