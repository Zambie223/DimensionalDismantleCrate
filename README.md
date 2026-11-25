# Dimensional Dismantle Crate

Dismantle crates upload their contents to the **Dimensional Depot** by themselves, at the same speed as a Dimensional Depot Uploader, whenever the depot has room. No more walking back to empty them.

- Each crate behaves like one uploader: one item every upload interval, so upload speed research applies.
- Items the depot can't take (it's full of them, or the game doesn't allow uploading them) stay in the crate, and the other items keep uploading. A crate disappears once it's empty, just as it does when you empty it by hand.
- Works only after the **Dimensional Depot research** is done and **at least one uploader is built**.
- Death crates are left alone unless you turn that on in Mod Options.
- **Optional** (all off by default):
  - when you dismantle with a full inventory, the items that don't fit go straight into the depot instead of a crate
  - dismantle refunds go to the depot first for players whose own "take from inventory before depot" setting is off
  - items the depot is full of go to the AWESOME Sink for points
  - a chat message says why crates aren't emptying
  - faster upload speeds: Instant, or as fast as all your uploaders together
- **Only the host or server needs the mod.** Players without it can join.

## Installation

You need Satisfactory 1.2.5.1 (build 520140) or newer.

**With Satisfactory Mod Manager (recommended):** search for *Dimensional Dismantle Crate* in [Satisfactory Mod Manager](https://docs.ficsit.app/satisfactory-modding/latest/ForUsers/SatisfactoryModManager.html), or open its page on the [Satisfactory Mod Repository](https://ficsit.app/mod/DimensionalDismantleCrate) and choose Install. The mod manager also installs Satisfactory Mod Loader (SML) and keeps the mod up to date.

**By hand**, from this repository's releases (you also need Satisfactory Mod Loader (SML) 3.12.1 or newer installed):

1. From the Releases page, download the zip for where it will run:
   - `DimensionalDismantleCrate-Windows.zip` for your game
   - `DimensionalDismantleCrate-WindowsServer.zip` or `DimensionalDismantleCrate-LinuxServer.zip` for a dedicated server
2. Extract it into `FactoryGame/Mods/DimensionalDismantleCrate/` inside the game or server folder, so that `DimensionalDismantleCrate.uplugin` sits directly in that folder.
3. Start the game or server. The mod appears under **Mods** in the main menu.

## Settings

Open **Mods → Dimensional Dismantle Crate** from the main menu or the pause menu. Changes apply immediately, without reloading the world.

| Setting | Default | What it does |
|---|---|---|
| Upload crates to the Dimensional Depot | On | Turns crate uploading on or off. The dismantle overflow setting below works either way. |
| Upload speed | Per crate | **Per crate:** every crate uploads like one uploader. **Shared:** all crates together upload like one uploader, taking turns. **Per stack slot:** every filled slot in a crate uploads like one uploader (much faster for big crates). **Instant:** crates empty as fast as the depot can take the items. **Shared by all uploaders:** all crates together upload like every uploader you have built, taking turns. Research and other mods that change the depot's upload speed apply in every mode except Instant. |
| Also upload death crates | Off | Also upload the crates players drop when they die. |
| Upload equipment from death crates | Off | Only matters when death crates are uploaded. While off, equipment and items that store something (fuel, ammo, ...) stay in the death crate. **Risk:** the depot only stores item counts, so anything an item stores is lost when it's uploaded. |
| Send dismantle overflow straight to the depot | Off | When you dismantle something and your inventory is full, the items that don't fit go straight into the depot, without the upload wait, as long as it has room. Your inventory is still filled first, and items the depot can't take still go into a crate. Needs the same research and uploader as crate uploading. |
| Dismantle refunds follow each player's depot setting | Off | When on, each player's own **take from inventory before depot** setting (in the inventory screen) also decides where their dismantle refunds go. With that setting off, the refund goes to the depot first, then to the inventory, then into a crate. With it on, nothing changes. **Note:** the game's default for that setting is off, so players who never changed it get depot-first refunds once you turn this on. Needs the same research and uploader as crate uploading. |
| Sink what the depot is full of | Off | Items in dismantle crates that the depot has no room for go to the AWESOME Sink for points instead of waiting in the crate, at the same speed as uploading. Needs at least one AWESOME Sink built. Death crates, equipment, items that store something and items worth no points are never sunk. Sunk items are gone. |
| Say why crates aren't emptying | Off | Posts a short chat message when items in crates can't upload, for example because the depot is full of them, or when no uploader is built yet. At most one message every 30 seconds, and the same item at most every 5 minutes. Every player sees the messages. |

Dismantle crates follow the same upload rules as the game's own uploads: anything the game lets you upload from your inventory can be uploaded from a crate.

## Multiplayer

- **Install the mod on the host or dedicated server.** Players don't need it, but it doesn't hurt if they have it.
- **The host's settings are used, or the server's on a dedicated server.** A joined player's own Mod Options have no effect on someone else's world.
- Crate contents are only sent to a player while they have that crate open (this is how the base game works), so the mod adds no network traffic for crates. Each uploaded item updates the depot count for every player, exactly like an uploader does.

### Dedicated servers

Dedicated servers have no menu. Use either of these:

- **Edit the config file.** It is `FactoryGame/Configs/DimensionalDismantleCrate.cfg` in the server's install folder, created on first start. `RateMode` is `0` (per crate), `1` (shared), `2` (per stack slot), `3` (instant) or `4` (shared by all uploaders):
  ```json
  {
      "Enabled": true,
      "RateMode": 0,
      "IncludeDeathCrates": false,
      "UploadDeathCrateEquipment": false,
      "SendDismantleOverflowToDepot": false,
      "RefundsFollowDepotPreference": false,
      "SinkWhenDepotFull": false,
      "ShowStuckNotices": false,
      "SML_ModVersion_DoNotChange": "0.3.0"
  }
  ```
  Then restart the server.
- **Use the console.** Run these commands from the Server Manager console, which needs admin rights:

  | Command | Effect |
  |---|---|
  | `DDC.Status` | Shows the settings, whether uploading is allowed yet (research and uploader), the upload speed, and the crates the mod sees. |
  | `DDC.Set RateMode Shared` | Changes and saves a setting. `RateMode` takes `PerCrate`, `Shared`, `PerSlot`, `Instant` or `PerUploader`; the other settings take `true` or `false`, e.g. `DDC.Set IncludeDeathCrates true`. |

## Limits and notes

- **Speed:** like an uploader, a crate uploads at most one item per game tick, and catches up after a hitch the same way. Very high upload speeds from other mods therefore top out at your frame rate (or the server's tick rate) per crate, or per filled slot in Per stack slot mode. Shared by all uploaders moves up to one item per uploader per tick. Instant moves up to 500 items per tick; emptying a large pile of crates that way cost up to about 1.2 ms per frame in testing.
- **Waiting crates:** a crate with nothing the depot can take checks again every 0.25 seconds, so it starts uploading up to 0.25 seconds after the depot has room.
- **Many crates:** 200 crates that can't upload anything cost about 0.1 ms per frame in testing.
- **Saves:** nothing is written into your save. An upload interval that was in progress when you saved restarts after loading, so at most one item's worth of time is lost. Removing the mod leaves crates as they are.
- **Other crates:** crates created by other mods without a dismantle or death crate type are left alone.
- **Network limit in multiplayer:** every item added to the depot is a network message to every player, the same as an uploader. So with players connected, the mod adds at most 3,000 items per second to the depot, shared by crate uploads, dismantle overflow and depot-first refunds; crates take turns while the limit holds them back. A very large dismantle puts the rest into a crate for a moment, and it is sent from there. In single player there is effectively no limit.
- **Dismantle overflow and saves:** if you save while overflow is still being sent from a crate, the rest uploads at normal crate speed after loading.
- **Sinking:** sunk items add their AWESOME Sink points. Sinking doesn't use the network limit.
- **How dismantles are caught:** the mod hooks the game function that turns dismantle leftovers into a crate, the server's dismantle function, and the function that adds items to an inventory (only to redirect refunds while a dismantle runs with the depot-first setting on; everything else uses the game's public functions). The inventory hook runs for every item added anywhere in the game, so it is kept to one check: in testing it added about 20-25 ns to each call. How often the game makes that call depends on the save; even 100,000 calls per second would add up to about 2.5 ms of CPU time per second of play, spread over all threads. If a game update changes how dismantle refunds are handled, the dismantle settings may stop working until the mod is updated.

## Status

Version 0.3.0 has been tested on game 1.2.5.1 with SML 3.12.1, using a late-game save with 16 uploaders. An automated in-game test of 45 checks, run on a development build of the same code, passed in single player, in a game hosted from the game itself with a joined player who had the mod, and on a Windows and a Linux dedicated server with a joined player (on each, once with the mod and once without it). The release build was then run in single player, in a hosted game and on both servers with a joined player who had the mod, and on the Linux server also with one who didn't, without errors from the mod, and its Mod Options menu was checked. The checks cover:
- upload speed in Per crate, Shared and Per stack slot mode, matching a vanilla uploader (240 items/min at full research); Shared by all uploaders at 16 uploaders' speed; Instant emptying crates within a few frames
- depot counts matching what left the crates, and items the depot is full of staying in the crate while the rest upload
- death crates ignored by default, uploaded with the option, and equipment kept back unless that option is on
- crates that wait while the depot is full and start again once it has room
- empty crates disappearing, turning the mod off, and 200 idle crates
- the multiplayer network limit (tested with a lowered limit): uploads stay within it and crates take turns evenly
- dismantle overflow, using the game's own dismantle with a full inventory for both foundations and buildings: a crate as before with the setting off; with it on, the overflow sent straight to the depot with no crate, the inventory filled first, items the depot is full of going into a crate, and items held back by the rate limit sent from the crate shortly after
- depot-first refunds, using the game's own dismantle: the refund goes to the depot for a player whose "take from inventory before depot" setting is off, to the inventory for a player who has it on, and to the inventory for everyone while the switch is off
- sinking: a dismantle crate's items the depot is full of are sunk, and a death crate is left alone
- chat notices: an item the depot is full of is named once and not repeated within 5 minutes, an item with room isn't reported, equipment a death crate keeps isn't reported, and the "no uploader built" message goes out once. The messages were seen on the screen of a joined player without the mod, on both the Windows and the Linux server.
- the cost of the inventory hook (see Limits and notes)

Not tested: a player without the mod joining someone who hosts from the game itself (only dedicated servers were tested with such a player), and an internet game across several PCs; all multiplayer tests ran on one PC.

## Building from source

1. Set up the [SML starter project](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/index.html) with the Unreal Engine version it requires.
2. Clone this repository into the starter project as `Mods/DimensionalDismantleCrate`.
3. Package it with Alpakit. Building the LinuxServer target also needs Unreal's Linux cross-compile toolchain.

`Tools/create_config_asset.py` creates or updates the Mod Options assets (`Content/DDCConfiguration.uasset` and the dropdown labels in `Content/DDCRateModeEnum.uasset`), and `Tools/verify_config_asset.py` checks them. Both run headless in the editor, with instructions at the top of each file. `Tools/make_icon.py` redraws the icon and needs Python with Pillow.

The automated in-game tests are compiled in only when `DDC_DEV_COMMANDS` is set to 1 in `Source/DimensionalDismantleCrate/DimensionalDismantleCrate.Build.cs` (it is 0 for releases). The dismantle steps of the test call a protected game function, so a test build also needs a `Config/AccessTransformers.ini` containing `[AccessTransformers]` and `Friend=(Class="UFGBuildGunStateDismantle", FriendClass="FDDCDevDismantleAccess")`; it is not part of the repository because release builds don't use it. `Tools/run_autotest.ps1` runs them in single player, `Tools/tests/ddc_listen_test.ps1` in a game hosted from the game itself with a second instance joined, and `Tools/tests/ddc_ds_test.sh` on a dedicated server; all three expect a test save with the Dimensional Depot researched and an uploader built, and their paths point at a local test setup that you will need to adjust.

## License

Copyright (C) 2026 Zambie223

Dimensional Dismantle Crate is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

## Responsible AI use disclosure

Most of the code, tooling, artwork and documentation in this mod, including the changelogs, were written by an AI coding assistant (Claude, made by Anthropic, used through Claude Code), working under the maintainer's direction. The maintainer set the requirements and design decisions. Correctness was checked with hands-on runs in a real Satisfactory game client and on Windows and Linux dedicated servers (see Status above), but not every line has been reviewed by a person. AI-written code can contain mistakes: review it before relying on it, and back up your saves.
