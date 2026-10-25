# DevKitApi — connect Claude Code to the ARK: Survival Evolved DevKit

DevKitApi lets an AI assistant (Claude Code) drive the ARK DevKit directly: find and load assets, read and write any
property, duplicate Blueprints, create and compile new ones, add variables and functions, write Blueprint graphs, run
Play In Editor and send console commands. You describe the mod in plain words, the AI does the clicking.

Everything in this repository was built and tested on the real DevKit (Unreal Engine 4.5.1, Wildcard branch). Mods made
with it include a rock cannon that buries a Rex under 100 boulders, a dance pistol with Blender-made dino dances, a whole
palace placed like a single foundation, and a Superman flight key.

It has two parts:

| Part | What it is | Where it runs |
|---|---|---|
| `plugin/` | A small editor plugin (a plain Windows DLL) that the DevKit loads at startup. It opens a local TCP socket and executes JSON commands on the editor's main thread. | Inside `UE4Editor.exe` |
| `mcp/` | A Python MCP server with one tool per command. Claude Code talks to it, it talks to the socket. | Next to Claude Code |

No engine source, no SDK and no rebuild of the DevKit are needed. The plugin never touches vanilla content: writes are
limited to the mod folders you list in a whitelist file.

---

## 1. Requirements

- Windows 10/11, 64-bit.
- **ARK: Survival Evolved Dev Kit** installed from the Epic Games Launcher. Below, `<DevKit>` is the folder that contains
  `Engine\` and `Projects\` (for example `C:\ARKDevKit\ARKEditor`). Tested on the DevKit build of October 2023
  (editor binaries dated 2023-10-11, before the Aquatica update). A newer DevKit build keeps the same engine
  (UE 4.5.1), so it will most likely work, but it has not been verified: if the plugin does not load, check
  `devkitapi.log` next to the DLL and open an issue.
- **Claude Code** (the CLI or the desktop app).
- **Python 3.10+** on the PATH, with the MCP package:

```bash
pip install mcp
```

- Only if you want to rebuild the DLL: Visual Studio 2022 or 2026 with the C++ workload (see section 7).

## 2. Install the plugin in the DevKit

1. Copy the `plugin\DevKitApi` folder into `<DevKit>\Engine\Plugins\`. You must end up with:

```
<DevKit>\Engine\Plugins\DevKitApi\DevKitApi.uplugin
<DevKit>\Engine\Plugins\DevKitApi\Binaries\Win64\UE4Editor-DevKitApi.dll
<DevKit>\Engine\Plugins\DevKitApi\Binaries\Win64\DevKitApi_whitelist.txt
```

2. Open `DevKitApi_whitelist.txt` and list the mod folders the AI is allowed to write to, one `/Game/...` root per line:

```
/Game/Mods/DevKitApiTest/
/Game/Mods/MyFirstMod/
```

   Anything outside these roots is refused (vanilla assets stay untouched). The file is read once at startup: edit it,
   then restart the DevKit.

3. Launch the DevKit as usual. The plugin is enabled by default. Two ways to confirm it loaded:
   - the Output Log shows a line `LogDevKitApi: [DevKitApi] loaded (socket 127.0.0.1:9877)`;
   - a file `devkitapi.log` appears next to the DLL.

4. From a terminal, ping the socket (the DevKit must be fully open):

```bash
python tools/ping.py
```

   Expected answer: `{"id":1,"ok":true,"result":"pong","editor_ready":true}`. `editor_ready:false` just means the
   editor is still loading.

## 3. Connect Claude Code

Register the MCP server once (replace the path with your clone of this repository):

```bash
claude mcp add devkit -- python C:\path\to\DevKitApi\mcp\devkit_mcp.py
```

Or add it by hand to `~/.claude.json` (or to a project `.mcp.json`):

```json
{
  "mcpServers": {
    "devkit": {
      "type": "stdio",
      "command": "python",
      "args": ["C:\\path\\to\\DevKitApi\\mcp\\devkit_mcp.py"],
      "env": { "ARK_DEVKIT_ROOT": "C:\\ARKDevKit\\ARKEditor" }
    }
  }
}
```

`ARK_DEVKIT_ROOT` is optional: it only tells the server where to find `ShooterGame.log`, which the Blueprint reading
tools use to recover outputs that the editor truncates. Two other optional variables: `DEVKITAPI_CACHE` (where T3D
exports are cached, default `<repo>\cache\t3d`) and `DEVKITAPI_OUT_ROOT` (the only folder where tools may write
`out_file` results, default the repository).

**Run Claude Code from this repository folder** (`cd` into your clone before starting `claude`): the `CLAUDE.md` at
the root tells Claude where mods live (`/Game/Mods/<ModName>/`), how the whitelist works, the standard
load → duplicate → edit → compile → save workflow, and the traps to avoid. Without it Claude still works, but it has to
discover these rules by trial and error.

Start a **new** Claude Code session (tools are discovered at startup), check with `claude mcp list` that `devkit` is
connected, and ask:

> Ping the DevKit and tell me if the editor is ready.

Claude should call `devkit_ping` and report `pong`.

## 4. Your first mod, in plain words

With the DevKit open and `/Game/Mods/MyFirstMod/` in the whitelist, try:

> Duplicate the Dodo (`/Game/PrimalEarth/Dinos/Dodo/Dodo_Character_BP`) into `/Game/Mods/MyFirstMod/BigDodo`,
> triple its max health, compile it and save it.

What Claude does behind the scenes: `devkit_load_asset` (a dino Blueprint takes 1 to 2 minutes to load the first
time), `devkit_duplicate_asset`, `devkit_get_property` to find the health value on the status component template,
`devkit_set_property`, `devkit_compile_blueprint`, `devkit_save_package`. The new asset appears in the Content Browser
immediately.

To test without cooking: press Play, open the console and type `cheat summon BigDodo_C` (the short class name works
as long as the Blueprint is loaded in the editor).

## 5. The tools

| Tool | What it does |
|---|---|
| `devkit_ping`, `devkit_editor_ready`, `devkit_log` | liveness, editor state, write a line to the Output Log |
| `devkit_exec` | run any editor console command and capture its output (`obj list`, `obj dump`, `obj export`, `ke * Event` in PIE...) |
| `devkit_find_object`, `devkit_load_asset` | find an object already in memory, or load an asset from disk |
| `devkit_get_property`, `devkit_set_property` | read / write any property by name through reflection, on a class default object or on a sub-object / instance (`cdo=false`) |
| `devkit_duplicate_asset`, `devkit_create_blueprint`, `devkit_add_component` | create content (whitelisted roots only) |
| `devkit_compile_blueprint`, `devkit_save_package` | compile (returns the compiler's errors and warnings) and save to `.uasset` |
| `devkit_add_variable`, `devkit_remove_variable`, `devkit_add_function`, `devkit_remove_graph` | Blueprint structure, including replicated variables and overrides of native events |
| `devkit_write_graph`, `devkit_paste_t3d`, `devkit_link_pins`, `devkit_set_pin_default`, `devkit_list_nodes` | write Blueprint graphs: nodes are generated as T3D text (the editor's own clipboard format), pasted, then linked by pin name |
| `devkit_read_blueprint`, `devkit_read_object` | read a whole Blueprint (parent, variables, components, overridden defaults, every graph as pseudo-code or as nodes/pins/links) without modifying anything |
| `devkit_pie`, `devkit_console_command` | start / stop Play In Editor, send a console command to a PIE world |
| `devkit_raw` | send any JSON request to the socket |

The socket protocol is one JSON object per line in, one per line out (`{"cmd":"ping"}` → `{"ok":true,"result":"pong"}`).
`tools/dk_socket.py` is a 30-line client you can use from your own scripts, and `tools/ping.py` a command-line tester.

## 6. How it works (for the curious)

- **Loading.** The DevKit automatically loads any folder under `Engine\Plugins` that contains a `.uplugin` with
  `EnabledByDefault` and a DLL exporting the module initialisation function. The DLL is modelled on Epic's own
  `BlankPlugin` shipped with the DevKit.
- **Talking to the engine.** The editor's DLLs export their C++ symbols, so engine functions are resolved by name at
  runtime. Everything that touches the editor is queued and executed on the main thread through a Windows message hook;
  the socket thread never calls the engine directly.
- **Reading and writing properties** goes through Unreal's reflection (`ExportText` / `ImportText` on the class default
  object), so every property the editor shows in Details is reachable by name.
- **Graphs** are read by exporting the Blueprint as T3D text and written by pasting T3D nodes back
  (`ImportNodesFromText`) and connecting pins through the K2 schema, exactly as a copy/paste in the editor would.
- **Safety.** Every write command checks the package path against the whitelist. Vanilla content is never writable.
  The socket listens on `127.0.0.1` only (no network exposure) and has no authentication: any program running on
  your own machine while the DevKit is open could send it commands, within the whitelist. Do not change the bind
  address to expose it on a network.

## 7. Rebuilding the DLL (optional)

The DLL in `plugin/` is prebuilt. To rebuild it from `src/`:

1. Install Visual Studio 2022 or 2026 with "Desktop development with C++".
2. Open `src\build.bat` and adjust the `vcvarsall.bat` path and `-vcvars_ver` to your installation if they differ.
3. Run `src\build.bat`. Output: `build\UE4Editor-DevKitApi.dll` and `.pdb`.
4. Check it: `pip install pefile` then `python tools\check_dll.py build\UE4Editor-DevKitApi.dll`.

Two things the build **must** keep, learned the hard way:

- **Static C runtime (`/MT`).** The DevKit ships an old `MSVCP140.dll` (VS2017) in `Projects\ShooterGame\Binaries\Win64`
  and the loader picks it first. A `/MD` build with a recent compiler crashes right after the module starts.
- **Resource `RCDATA 191 = "0"`** (see `src\DevKitApi.rc`). The editor's "stale module" check reads this resource, not
  the VERSIONINFO block. Without it the plugin is silently refused.

## 8. Things that bit us (read before your first real mod)

- **Never compile a Blueprint while Play In Editor is running.** It raises an assertion and the editor crashes when PIE
  stops. Stop PIE first.
- **Re-test compilation after a cold restart.** A Blueprint can compile fine in the session that created it and fail
  when the editor reloads it. Close the editor, reopen, load the asset, compile again.
- **A wrong pin name is a silently lost link.** Node reconstruction keeps links by pin name; a typo compiles with zero
  errors and the node just does nothing. Always check with `devkit_list_nodes` that your pins show links.
- **Replicated variables: pass `rep_condition: 0`** to `devkit_add_variable`, otherwise the replication condition is
  left uninitialised and the variable may never reach the client.
- **The editor lies about cooked mods.** Values stored only in editor data (for example the scale on a component
  template) can be lost after cooking. Set important values through function calls in the graph.
- **`set_property` on a struct merges, on an array it replaces.** Re-read the value after writing.
- **Loading a dino Blueprint takes 1 to 2 minutes** the first time. Duplicating a PrimalGameData can freeze the editor
  for a minute and time out, but the copy succeeds: wait, then `devkit_find_object`.
- **ARK's PIE runs two worlds** when "Run Dedicated Server" is checked (a server world and a client world). Console
  commands and `ke *` events hit both. Uncheck it to test a single-player (solo) setup.

## 9. Repository layout

```
CLAUDE.md              rules Claude Code reads automatically when started from this folder (mod folders, whitelist, workflow, traps)
plugin/DevKitApi/      ready-to-copy plugin folder (uplugin, DLL, whitelist)
src/                   DLL source (DevKitApi.cpp, DevKitApi_Phase4.h, DevKitApi.rc) and build.bat
mcp/devkit_mcp.py      MCP server for Claude Code
tools/t3d_graph.py     T3D parser → pseudo-code (used by the read tools)
tools/t3d_write.py     graph spec → T3D generator (used by devkit_write_graph)
tools/dk_socket.py     minimal socket client for your own scripts
tools/ping.py          command-line tester
tools/check_dll.py     verifies a rebuilt DLL
```

## License

MIT. Made by OPALL with Claude Code. ARK: Survival Evolved and the ARK Dev Kit are property of Studio Wildcard;
this project is not affiliated with them.
