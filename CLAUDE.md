# DevKitApi — instructions for Claude Code

You are driving the ARK: Survival Evolved DevKit (Unreal Engine 4.5.1) through the `devkit` MCP server of this
repository. The DevKit must be open. Start every session with `devkit_ping`; `editor_ready:false` means the editor is
still loading, wait and retry.

## Where things live

- Asset paths are Unreal paths: `/Game/<folder>/<Asset>.<Asset>` (package path `/Game/<folder>/<Asset>`, object name
  repeated after the dot). `/Game/` maps to `<DevKit>\Projects\ShooterGame\Content\` on disk.
- Vanilla content is under `/Game/PrimalEarth/...` (dinos in `/Game/PrimalEarth/Dinos/<Name>/`, items, buffs, weapons,
  damage types in `/Game/PrimalEarth/CoreBlueprints/...`). It is **read-only**: never try to save or modify it.
- **Every mod lives in its own folder `/Game/Mods/<ModName>/`.** All new or duplicated assets go there. A generated
  Blueprint class is `<Asset>_C` (for example `/Game/Mods/MyMod/BigDodo.BigDodo_C`).
- Writes are only allowed under the roots listed in `DevKitApi_whitelist.txt` next to the plugin DLL
  (`<DevKit>\Engine\Plugins\DevKitApi\Binaries\Win64\`). If a write is refused with "whitelist", tell the user to add
  the line `/Game/Mods/<ModName>/` to that file and restart the DevKit. Do not try to work around the whitelist.
- Before the first write in a mod folder, ask the user for the mod name if they did not give one, and use that single
  folder for all its assets.

## Every mod needs a PrimalGameData (PGD)

A mod is not a mod without a `PrimalGameData_BP_<ModName>` Blueprint in its folder. The DevKit's cooker starts from it:
it decides what gets packaged, and the game reads it to know what the mod adds. Create it first, in every mod, even
for a single item.

- **How to create it**: duplicate the small example PGD shipped with the DevKit,
  `/Game/Mods/GigaRocket/PrimalGameData_BP_GigaRocket.PrimalGameData_BP_GigaRocket`, into
  `/Game/Mods/<ModName>/PrimalGameData_BP_<ModName>`, then set `ModName` and `ModDescription` (English). Do **not**
  duplicate the vanilla `/Game/PrimalEarth/CoreBlueprints/PrimalGameData_BP`: it is huge and drags the whole game with it.
  Duplicating a PGD may time out ("editor busy") while the editor freezes for a minute: the copy still succeeds, wait
  and `devkit_find_object` it.
- **What to put in it** (all are arrays on the PGD's class default object):
  - `AdditionalEngramBlueprintClasses`: the `EngramEntry_*` of every craftable item/weapon/structure the mod adds.
  - `AdditionalStructuresToPlace`: placeable structures.
  - `ServerExtraWorldSingletonActorClasses`: an Actor class spawned once per server (the way to give every player a
    permanent buff, run a timer, bind to game events).
  - `ExtraResources`: any asset nothing else references (a custom dino with no engram, a montage, an actor spawned by
    name). If an asset is not referenced by the PGD directly or indirectly, **it is not cooked** and is missing in game.
  - `AdditionalDefaultBuffs` works only for the first ("primary") mod of a server; for a stackable mod use a singleton
    actor that calls `StaticAddBuff` on players instead.
- **Never touch `MasterItemList`** on a mod PGD (writing it serializes all 500 vanilla items and breaks the game).
  Avoid `Remap_Items` / `Remap_Engrams` unless the user explicitly wants to replace a vanilla item: two mods remapping
  the same item conflict. Prefer adding new items.
- Cooking and uploading to the Workshop are done by the user in the DevKit (Steam Upload tool, pick the mod folder).

## Standard workflow for a mod

1. `devkit_load_asset` the vanilla asset you start from (a dino Blueprint can take 1 to 2 minutes; use a long timeout).
2. `devkit_duplicate_asset` into `/Game/Mods/<ModName>/<NewName>` (never edit the vanilla asset in place).
3. `devkit_get_property` to read current values before changing them, then `devkit_set_property`. Properties of a
   component live on the component template (for example a dino's health is on its status component
   `<BP>_C:DinoCharacterStatusComponent_..._0`, read with `cdo=false`); use `devkit_exec` with
   `obj list class=<ComponentClass> package=<package>` to find the exact template name.
4. `devkit_compile_blueprint` and read its `errors`/`warnings`.
5. `devkit_save_package`. Re-read a value after saving if it matters.
6. Reference the new asset from the PGD (section above), compile and save the PGD too.
7. To test without cooking: Play In Editor (`devkit_pie start`), then `devkit_console_command` with
   `cheat summon <Asset>_C` or `cheat giveitem "Blueprint'/Game/Mods/<ModName>/<Item>.<Item>'" 1 0 0`. The short class
   name works only if the asset is loaded in the editor. `cheat god` keeps the tester alive; `cheat gmsummon "<Dino>_C" 50`
   spawns a tamed level-50 dino that does not fight back.

## Recipes that are known to work

Learned on real mods built with this tool. Start from a vanilla asset that already does something close, read it with
`devkit_read_blueprint` (parent, overridden defaults, graphs as pseudo-code), and change as little as possible.

- **New item**: duplicate the closest vanilla `PrimalItem_*`, set `DescriptiveNameBase`, `ItemDescription`,
  `BaseCraftingResourceRequirements` (write the whole array). Craftable in the inventory: `CraftingRequiresInventoryComponent=()`.
- **New engram** (`EngramEntry_*`, duplicate a vanilla one): `BluePrintEntry` → your item, `RequiredCharacterLevel`,
  `RequiredEngramPoints`, and **check the inherited values**: `EngramRequirementSets=()` (otherwise another engram is
  required first and the button stays greyed), `bGiveBlueprintToPlayerInventory=True` for hand-crafted items. An engram
  must be learned in the inventory before the item exists anywhere; creative mode hides these bugs. Engrams and PGD
  changes cannot be tested in PIE (PIE uses the vanilla PGD): they need a cook.
- **New weapon**: duplicate a vanilla `Weap*` with an empty EventGraph (for example
  `/Game/PrimalEarth/CoreBlueprints/Weapons/WeapMachinedPistol`), set `ProjectileClass` or `InstantConfig.DamageType`,
  rewrite `WeaponConfig` in full. Infinite ammo without reload: `bInfiniteAmmo=True, bNeverReload=True,
  bUsesAmmo=False, AmmoPerClip=9999` **plus** `bUseBPStartEquippedNotify=True` with a `BPStartEquippedNotify` graph
  that calls `SetAmmoInClip(9999)` then `ForceUpdateClientAmmo` (a crafted weapon starts with an empty clip and never
  fires otherwise). Silence it with `FireSound`/`MuzzleFX`/`FireCameraShake`=None if it fires fast.
- **Zero-damage "effect" weapon**: duplicate `/Game/PrimalEarth/CoreBlueprints/DamageTypes/DmgType_Instant`, set
  `bDontActuallyDealDamage=True`, `bOnlyGiveBuffToDino=True`, `BuffToGiveVictimCharacter` → your buff, and
  `bUseBPAdjustAggro=True` with a `BPAdjustAggro` override returning 0 (otherwise the dino still aggros the shooter).
- **Buffs** are the universal tool: a Blueprint child of `PrimalBuff` attached to a character. Useful defaults:
  `DeactivateAfterTime` (0 = permanent), `bReplicates`, `bHideBuffFromHUD`, `bPreventOnBossDino`, `AddBuffMaxNumStacks`.
  Slow or freeze a dino with `bModifyMaxSpeed=True` + `UnsubmergedMaxSpeedModifier` (0 = frozen), not with stat
  modifiers. A buff that must react to every hit: `bAddResetsBuffTime=False`, `bAddReactivates=True`,
  `bUseBPActivated=True`, `AddBuffMaxNumStacks=100` (then `BPActivated` runs on each hit). Apply from a graph with
  `StaticAddBuff`, remove with `DeactivateBuffs`.
- **Custom animation on a dino**: import the FBX on the dino's skeleton, create an AnimMontage (slot `FullBodySlot`,
  loop by `CompositeSections` Default→Default), then one buff per animation whose graph calls `PlayAnimMontage` on
  `ReceiveBeginPlay` (local on every client). A NetMulticast RPC in a tool-made buff does not reach clients; the
  buff-per-animation pattern does.
- **New dino**: duplicate `<Dino>_Character_BP`; stats are on its status component template; `DescriptiveName` on the
  class default object. Reparenting works by setting `ParentClass` on the Blueprint and compiling.
- **Composite structure** (a whole building placed as one foundation): duplicate a vanilla foundation Blueprint and add
  `StaticMeshComponent`s with `devkit_add_component` + `set_property` on each template (`cdo=false`); item = duplicate
  of the foundation's item with `StructureToBuild` → your Blueprint; add it to `AdditionalStructuresToPlace`.
- **Give every player something** (key binding, permanent effect) from a stackable mod: singleton actor in
  `ServerExtraWorldSingletonActorClasses` with a timer that `StaticAddBuff`s players who lack the buff.
- **Finding things**: `devkit_exec "obj list class=Blueprint"` is too big; prefer `devkit_find_object` with a guessed
  path, or `obj list class=<Class> package=<package>` for templates. ARK's native Blueprint library `VictoryCore`
  (`/Script/ShooterGame.VictoryCore`) has hundreds of helpers (ground location, octree overlaps, spawn deferred, cooldowns).

## Rules that avoid wasted hours

- **Never compile a Blueprint while PIE is running**: stop PIE first (`devkit_pie stop`), otherwise the editor crashes.
- **Never run a write command during the user's PIE session**; ask before stopping their session.
- `devkit_set_property` on a struct merges fields (write explicitly every field you want changed, including `False`);
  on an array it replaces the whole array (re-read it and write it back in full).
- A property read back as empty (`''`) means False / 0 / empty, not "inherited default".
- Graph writing (`devkit_write_graph`, `devkit_link_pins`): a wrong pin name silently drops the link and still compiles.
  After pasting, run `devkit_list_nodes` and check that the pins you linked show links. A node calling a Blueprint-pure
  function has no exec pins: do not put it in the exec chain. Prefer changing default values over writing graphs when
  a property does what the user wants.
- `devkit_add_variable` with `replicated: true` must also pass `rep_condition: 0`.
- Values set on component templates can be lost after cooking; set them through function calls in the graph if they
  must survive cooking. The editor (PIE) proves logic, only a cooked mod proves what ships.
- Replication check without cooking: Play with "Number of Players" = 2 in the PIE settings. Solo play (no dedicated
  server) runs client and server on the same object: a server RPC must keep its own state variables.
- Loading a dino Blueprint takes 1 to 2 minutes; AnimBlueprints must be compiled by the user in the editor, never with
  `devkit_compile_blueprint` (it freezes the DevKit).
- Keep answers short and report what was actually verified (compiled, saved, tested in PIE) versus assumed.
