# Changelog

All notable changes after the first public release are listed here.

## 0.2.0

- **New setting: Send dismantle overflow straight to the depot** (off by default). When you dismantle something and your inventory is full, the items that don't fit go straight into the Dimensional Depot instead of a dismantle crate, without the upload wait, as long as the depot has room. Your inventory is still filled first, and items the depot can't take still go into a crate. In multiplayer, very large amounts arrive over a few seconds.
- The "Upload crates to the Dimensional Depot" setting now only turns crate uploading on or off; the new setting works either way.

## 0.1.1

- **Now on the Satisfactory Mod Repository**, so it can be installed and updated with Satisfactory Mod Manager.
- **Lower CPU cost for crates that can't upload anything.** A crate whose items the depot won't take (it's full of them, or death-crate equipment that stays behind) now checks again every 0.25 seconds instead of every frame (200 such crates went from about 0.18 ms to 0.1 ms per frame in testing). Uploading can therefore start up to 0.25 seconds later once the depot has room; the upload speed after that is unchanged.
- **Lower cost with very many crates.** Finding crates the mod already knows about no longer slows down as the number of crates grows.
