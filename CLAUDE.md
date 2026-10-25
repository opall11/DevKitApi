# DevKitApi — instructions for Claude Code

You are driving the ARK: Survival Evolved DevKit (Unreal Engine 4.5.1) through the `devkit` MCP server of this
repository. The DevKit must be open. Start every session with `devkit_ping`; `editor_ready:false` means the editor is
still loading, wait and retry.

## Where things live

- Asset paths are Unreal paths: `/Game/<folder>/<Asset>.<Asset>` (package path `/Game/<folder>/<Asset>`, object name
  repeated after the dot). `/Game/` maps to `<DevKit>\Projects\ShooterGame\Content\` on disk.
- Vanilla content is under `/Game/PrimalEarth/...` (dinos in `/Game/PrimalEarth/Dinos/<Name>/`, items and buffs in
  `/Game/PrimalEarth/CoreBlueprints/...`). It is **read-only**: never try to save or modify it.
- **Every mod lives in its own folder `/Game/Mods/<ModName>/`.** All new or duplicated assets go there. A generated
  Blueprint class is `<Asset>_C` (for example `/Game/Mods/MyMod/BigDodo.BigDodo_C`).
- Writes are only allowed under the roots listed in `DevKitApi_whitelist.txt` next to the plugin DLL
  (`<DevKit>\Engine\Plugins\DevKitApi\Binaries\Win64\`). If a write is refused with "whitelist", tell the user to add
  the line `/Game/Mods/<ModName>/` to that file and restart the DevKit. Do not try to work around the whitelist.
- Before the first write in a mod folder, ask the user for the mod name if they did not give one, and use that single
  folder for all its assets.

## Standard workflow for a mod

1. `devkit_load_asset` the vanilla asset you start from (a dino Blueprint can take 1 to 2 minutes; use a long timeout).
2. `devkit_duplicate_asset` into `/Game/Mods/<ModName>/<NewName>` (never edit the vanilla asset in place).
3. `devkit_get_property` to read current values before changing them, then `devkit_set_property`. Properties of a
   component live on the component template (for example a dino's health is on its status component
   `<BP>_C:DinoCharacterStatusComponent_..._0`, read with `cdo=false`); use `devkit_exec` with
   `obj list class=<ComponentClass> package=<package>` to find the exact template name.
4. `devkit_compile_blueprint` and read its `errors`/`warnings`.
5. `devkit_save_package`. Re-read a value after saving if it matters.
6. To test without cooking: Play In Editor (`devkit_pie start`), then `devkit_console_command` with
   `cheat summon <Asset>_C` or `cheat giveitem "Blueprint'/Game/Mods/<ModName>/<Item>.<Item>'" 1 0 0`. The short class
   name works only if the asset is loaded in the editor.

A mod that must be cooked and shipped needs a PrimalGameData Blueprint in the mod folder (duplicate a small vanilla one,
for example `/Game/Mods/GigaRocket/PrimalGameData_BP_GigaRocket` if present) and references to the new assets
(`AdditionalEngramBlueprintClasses` for items, `ExtraResources` for assets nothing else references). Never set
`MasterItemList` on a mod's PrimalGameData. Cooking and uploading are done by the user in the DevKit UI.

## Rules that avoid wasted hours

- **Never compile a Blueprint while PIE is running**: stop PIE first (`devkit_pie stop`), otherwise the editor crashes.
- **Never run a write command during the user's PIE session**; ask before stopping their session.
- `devkit_set_property` on a struct merges fields (write explicitly every field you want changed, including `False`);
  on an array it replaces the whole array (re-read it and write it back in full).
- A property read back as empty (`''`) means False / 0 / empty, not "inherited default".
- Graph writing (`devkit_write_graph`, `devkit_link_pins`): a wrong pin name silently drops the link and still compiles.
  After pasting, run `devkit_list_nodes` and check that the pins you linked show links. Prefer changing default values
  over writing graphs when a Blueprint property does what the user wants.
- `devkit_add_variable` with `replicated: true` must also pass `rep_condition: 0`.
- Values set on component templates can be lost after cooking; set them through function calls in the graph if they
  must survive cooking.
- Duplicating a PrimalGameData may time out ("editor busy") while the editor freezes for a minute: the copy still
  succeeds, wait and `devkit_find_object` it.
- Keep answers short and report what was actually verified (compiled, saved, tested in PIE) versus assumed.
