# Changelog

All notable changes after the first public release are listed here.

## 0.3.0

- **Needs game version 1.2.5.1 or newer** (and Satisfactory Mod Loader 3.12.1 or newer).
- **Two new upload speeds:**
  - **Instant:** crates empty as fast as the depot can take the items. In multiplayer that is up to 3,000 items per second.
  - **Shared by all uploaders:** all crates together upload as fast as every uploader you have built, taking turns.
- **New setting: Sink what the depot is full of** (off by default). Items in dismantle crates that the depot has no room for go to the AWESOME Sink for points instead of waiting in the crate. Needs at least one AWESOME Sink built. Death crates, equipment, items that store something and items worth no points are never sunk.
- **New setting: Dismantle refunds follow each player's depot setting** (off by default). When it's on, a player's own "take from inventory before depot" setting in the inventory screen also decides where their dismantle refunds go. With that setting off, refunds go to the depot first, then the inventory, then a crate. Note: the game's default for that setting is off, so players who never changed it get depot-first refunds.
- **New setting: Say why crates aren't emptying** (off by default). Posts a short chat message when items in crates can't upload, for example because the depot is full of them, or when no uploader is built yet. At most one message every 30 seconds, and the same item at most every 5 minutes.
- Crate uploads, dismantle overflow and depot-first refunds now share one network limit of 3,000 items per second in multiplayer (effectively no limit in single player). Before, only dismantle overflow was limited. While the limit holds uploads back, crates take turns.

## 0.2.0

- **New setting: Send dismantle overflow straight to the depot** (off by default). When you dismantle something and your inventory is full, the items that don't fit go straight into the Dimensional Depot instead of a dismantle crate, without the upload wait, as long as the depot has room. Your inventory is still filled first, and items the depot can't take still go into a crate. In multiplayer, very large amounts arrive over a few seconds.
- The "Upload crates to the Dimensional Depot" setting now only turns crate uploading on or off; the new setting works either way.

## 0.1.1

- **Now on the Satisfactory Mod Repository**, so it can be installed and updated with Satisfactory Mod Manager.
- **Lower CPU cost for crates that can't upload anything.** A crate whose items the depot won't take (it's full of them, or death-crate equipment that stays behind) now checks again every 0.25 seconds instead of every frame (200 such crates went from about 0.18 ms to 0.1 ms per frame in testing). Uploading can therefore start up to 0.25 seconds later once the depot has room; the upload speed after that is unchanged.
- **Lower cost with very many crates.** Finding crates the mod already knows about no longer slows down as the number of crates grows.
