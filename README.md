# Dimensional Dismantle Crate

Dismantle crates upload their contents to the **Dimensional Depot** by themselves, at the same speed as a Dimensional Depot Uploader, whenever the depot has room. No more walking back to empty them.

- Each crate behaves like one uploader: one item every upload interval, so upload speed research applies.
- Items the depot can't take (it's full of them, or the game doesn't allow uploading them) stay in the crate, and the other items keep uploading. A crate disappears once it's empty, just as it does when you empty it by hand.
- Works only after the **Dimensional Depot research** is done and **at least one uploader is built**.
- Death crates are left alone unless you turn that on in Mod Options.
- **Only the host or server needs the mod.** Players without it can join.

## Installation

You need Satisfactory 1.2 (build 517554 or newer).

**With Satisfactory Mod Manager (recommended):** search for *Dimensional Dismantle Crate* in [Satisfactory Mod Manager](https://docs.ficsit.app/satisfactory-modding/latest/ForUsers/SatisfactoryModManager.html), or open its page on the [Satisfactory Mod Repository](https://ficsit.app/mod/DimensionalDismantleCrate) and choose Install. The mod manager also installs Satisfactory Mod Loader (SML) and keeps the mod up to date.

**By hand**, from this repository's releases (you also need Satisfactory Mod Loader (SML) 3.12 or newer installed):

1. From the Releases page, download the zip for where it will run:
   - `DimensionalDismantleCrate-Windows.zip` for your game
   - `DimensionalDismantleCrate-WindowsServer.zip` or `DimensionalDismantleCrate-LinuxServer.zip` for a dedicated server
2. Extract it into `FactoryGame/Mods/DimensionalDismantleCrate/` inside the game or server folder, so that `DimensionalDismantleCrate.uplugin` sits directly in that folder.
3. Start the game or server. The mod appears under **Mods** in the main menu.

## Settings

Open **Mods → Dimensional Dismantle Crate** from the main menu or the pause menu. Changes apply immediately, without reloading the world.

| Setting | Default | What it does |
|---|---|---|
| Upload crates to the Dimensional Depot | On | Turns the mod on or off. |
| Upload speed | Per crate | **Per crate:** every crate uploads like one uploader. **Shared:** all crates together upload like one uploader, taking turns. **Per stack slot:** every filled slot in a crate uploads like one uploader (much faster for big crates). Research and other mods that change the depot's upload speed apply in every mode. |
| Also upload death crates | Off | Also upload the crates players drop when they die. |
| Upload equipment from death crates | Off | Only matters when death crates are uploaded. While off, equipment and items that store something (fuel, ammo, ...) stay in the death crate. **Risk:** the depot only stores item counts, so anything an item stores is lost when it's uploaded. |

Dismantle crates follow the same upload rules as the game's own uploads: anything the game lets you upload from your inventory can be uploaded from a crate.

## Multiplayer

- **Install the mod on the host or dedicated server.** Players don't need it, but it doesn't hurt if they have it.
- **The host's settings are used, or the server's on a dedicated server.** A joined player's own Mod Options have no effect on someone else's world.
- Crate contents are only sent to a player while they have that crate open (this is how the base game works), so the mod adds no network traffic for crates. Each uploaded item updates the depot count for every player, exactly like an uploader does.

### Dedicated servers

Dedicated servers have no menu. Use either of these:

- **Edit the config file.** It is `FactoryGame/Configs/DimensionalDismantleCrate.cfg` in the server's install folder, created on first start. `RateMode` is `0` (per crate), `1` (shared) or `2` (per stack slot):
  ```json
  {
      "Enabled": true,
      "RateMode": 0,
      "IncludeDeathCrates": false,
      "UploadDeathCrateEquipment": false,
      "SML_ModVersion_DoNotChange": "0.1.1"
  }
  ```
  Then restart the server.
- **Use the console.** Run these commands from the Server Manager console, which needs admin rights:

  | Command | Effect |
  |---|---|
  | `DDC.Status` | Shows the settings, whether uploading is allowed yet (research and uploader), the upload speed, and the crates the mod sees. |
  | `DDC.Set RateMode Shared` | Changes and saves a setting. `RateMode` takes `PerCrate`, `Shared` or `PerSlot`; the other settings take `true` or `false`, e.g. `DDC.Set IncludeDeathCrates true`. |

## Limits and notes

- **Speed:** like an uploader, a crate uploads at most one item per game tick, and catches up after a hitch the same way. Very high upload speeds from other mods therefore top out at your frame rate (or the server's tick rate) per crate, or per filled slot in Per stack slot mode.
- **Waiting crates:** a crate with nothing the depot can take checks again every 0.25 seconds, so it starts uploading up to 0.25 seconds after the depot has room.
- **Many crates:** 200 crates that can't upload anything cost about 0.1 ms per frame in testing.
- **Saves:** nothing is written into your save. An upload interval that was in progress when you saved restarts after loading, so at most one item's worth of time is lost. Removing the mod leaves crates as they are.
- **Other crates:** crates created by other mods without a dismantle or death crate type are left alone.

## Status

Version 0.1.1 has been tested on Windows in single player and on Windows and Linux dedicated servers with a joined player (with and without the mod), using a late-game save with 16 uploaders. An automated in-game test of 19 checks passed in every one of these setups, and the Mod Options menu was checked in the release build. The checks cover:
- upload speed in all three modes, matching a vanilla uploader (240 items/min at full research)
- depot counts matching what left the crates, and items the depot is full of staying in the crate while the rest upload
- death crates ignored by default, uploaded with the option, and equipment kept back unless that option is on
- crates that wait while the depot is full and start again once it has room
- empty crates disappearing, turning the mod off, and 200 idle crates
- the Mod Options menu, the config file and resetting to defaults

It has not been tested in an internet game across several PCs; all multiplayer tests ran on one PC.

## Building from source

1. Set up the [SML starter project](https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/index.html) with the Unreal Engine version it requires.
2. Clone this repository into the starter project as `Mods/DimensionalDismantleCrate`.
3. Package it with Alpakit. Building the LinuxServer target also needs Unreal's Linux cross-compile toolchain.

`Tools/create_config_asset.py` creates or updates the Mod Options assets (`Content/DDCConfiguration.uasset` and the dropdown labels in `Content/DDCRateModeEnum.uasset`), and `Tools/verify_config_asset.py` checks them. Both run headless in the editor, with instructions at the top of each file. `Tools/make_icon.py` redraws the icon and needs Python with Pillow.

The automated in-game tests are compiled in only when `DDC_DEV_COMMANDS` is set to 1 in `Source/DimensionalDismantleCrate/DimensionalDismantleCrate.Build.cs` (it is 0 for releases). `Tools/run_autotest.ps1` runs them in single player and `Tools/tests/ddc_ds_test.sh` on a dedicated server; both expect a test save with the Dimensional Depot researched and an uploader built, and their paths point at a local test setup that you will need to adjust.

## License

Copyright (C) 2026 Zambie223

Dimensional Dismantle Crate is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

## Responsible AI use disclosure

Most of the code, tooling and documentation in this mod were written by an AI coding assistant (Claude, made by Anthropic, used through Claude Code), working under the maintainer's direction. The maintainer set the requirements and design decisions. Correctness was checked with hands-on runs in a real Satisfactory game client and on Windows and Linux dedicated servers (see Status above), but not every line has been reviewed by a person. AI-written code can contain mistakes: review it before relying on it, and back up your saves.
