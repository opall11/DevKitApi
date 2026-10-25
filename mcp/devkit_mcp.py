#!/usr/bin/env python3
r"""devkit_mcp.py - serveur MCP (stdio) qui pilote le DevKit ARK via le plugin DevKitApi (socket 127.0.0.1:9877).

Registered in Claude Code (claude mcp add devkit -- python <repo>\mcp\devkit_mcp.py), or in ~/.claude.json :
  {"type":"stdio","command":"python","args":["<repo>\\mcp\\devkit_mcp.py"]}

Environment (optional) :
  ARK_DEVKIT_ROOT    DevKit install folder (the one that contains Engine\ and Projects\). Used to read ShooterGame.log.
  DEVKITAPI_OUT_ROOT folder under which out_file paths are allowed (default: this repository)
  DEVKITAPI_CACHE    cache folder for T3D exports (default: <repo>\cache\t3d)

Protocole DLL : une requete JSON par ligne -> une reponse JSON par ligne
  {"id":1,"cmd":"ping"}  ->  {"id":1,"ok":true,"result":"pong","editor_ready":true}
Chaque outil renvoie la reponse JSON brute de la DLL (texte), ou un message d'erreur clair si le DevKit ne repond pas.
"""
import itertools
import json
import os
import re
import socket
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import t3d_graph  # noqa: E402  (tools/t3d_graph.py)
import t3d_write  # noqa: E402  (tools/t3d_write.py)

try:                                   # mcp 1.x
    from mcp.server.fastmcp import FastMCP
except ImportError:                    # mcp 2.x : FastMCP renomme MCPServer
    from mcp.server.mcpserver import MCPServer as FastMCP

HOST, PORT = "127.0.0.1", 9877
_ids = itertools.count(1)

mcp = FastMCP("devkit", instructions=(
    "Drives the ARK: Survival Evolved DevKit (UE 4.5.1) through the DevKitApi plugin. "
    "The DevKit must be open; start with devkit_ping (editor_ready=false = still loading). "
    "Asset paths are Unreal paths: /Game/<folder>/<Asset>.<Asset>; vanilla content under /Game/PrimalEarth/ is read-only. "
    "Every mod lives in its own folder /Game/Mods/<ModName>/ and writes are only accepted under the roots listed in "
    "DevKitApi_whitelist.txt next to the plugin DLL (a refused write says 'whitelist': ask the user to add the mod root "
    "and restart the DevKit). Workflow: load_asset -> duplicate_asset into /Game/Mods/<ModName>/ -> get/set_property -> "
    "compile_blueprint -> save_package; test in PIE with 'cheat summon <Asset>_C'. Never compile while PIE is running. "
    "See CLAUDE.md in the repository for the full rules."
))


def _send(payload: dict, timeout: float = 60.0) -> str:
    payload = dict(payload)
    payload.setdefault("id", next(_ids))
    line = json.dumps(payload, ensure_ascii=False) + "\n"
    try:
        with socket.create_connection((HOST, PORT), timeout=timeout) as s:
            s.sendall(line.encode("utf-8"))
            buf = b""
            while b"\n" not in buf:
                chunk = s.recv(1 << 16)
                if not chunk:
                    break
                buf += chunk
    except ConnectionRefusedError:
        return json.dumps({"ok": False, "error": f"connexion refusee sur {HOST}:{PORT} : le DevKit ne tourne pas ou le plugin DevKitApi n'est pas charge"})
    except socket.timeout:
        return json.dumps({"ok": False, "error": "timeout : socket ouvert mais pas de reponse (thread principal de l'editeur occupe ?)"})
    except OSError as e:
        return json.dumps({"ok": False, "error": f"erreur socket : {e}"})
    return buf.decode("utf-8", "replace").strip()


@mcp.tool()
def devkit_ping() -> str:
    """Ping du plugin DevKitApi. Renvoie pong + editor_ready (true quand GEditor existe et que l'editeur a fini de demarrer)."""
    return _send({"cmd": "ping"})


@mcp.tool()
def devkit_editor_ready() -> str:
    """Indique si l'editeur est pret (execute sur le thread principal de l'editeur, prouve que la file de commandes fonctionne)."""
    return _send({"cmd": "editor_ready"})


@mcp.tool()
def devkit_log(text: str) -> str:
    """Ecrit une ligne '[DevKitApi] <text>' dans l'Output Log du DevKit (categorie LogDevKitApi)."""
    return _send({"cmd": "log", "text": text})


@mcp.tool()
def devkit_exec(command: str, to_log: bool = True, world_path: str = "") -> str:
    """Execute une commande console de l'editeur (UUnrealEdEngine::Exec), ex. 'DumpConsoleCommands', 'obj list class=Blueprint', 'stat unit'.
    Renvoie handled (bool) et la sortie texte capturee (tronquee a 64 Ko). to_log=true recopie aussi la sortie dans l'Output Log.
    world_path (optionnel) = chemin d'un UWorld precis, ex. le monde PIE '/Game/Maps/UEDPIE_0_TheIsland.TheIsland' (trouve par
    'obj list class=World'), pour les commandes qui agissent sur un monde pendant un Play In Editor : 'ke * MonEvent' (appelle
    l'event/fonction MonEvent sur tous les acteurs), 'summon X_C'... Sans world_path : GWorld (= le monde EDITEUR, meme en PIE)."""
    req = {"cmd": "exec", "command": command, "to_log": to_log}
    if world_path:
        req["world_path"] = world_path
    return _send(req)


@mcp.tool()
def devkit_find_object(path: str, class_name: str = "") -> str:
    """Cherche un objet DEJA CHARGE par chemin complet (StaticFindObject), ex. '/Script/Engine.Actor' ou
    '/Game/PrimalEarth/CoreBlueprints/Items/PrimalItemResource_Wood.PrimalItemResource_Wood_C'.
    class_name (optionnel) = chemin de classe pour filtrer, ex. '/Script/CoreUObject.Class'. Renvoie found, path, class, name, flags."""
    req = {"cmd": "find_object", "path": path}
    if class_name:
        req["class"] = class_name
    return _send(req)


@mcp.tool()
def devkit_load_asset(path: str) -> str:
    """Charge un asset depuis le disque si necessaire (StaticLoadObject) puis renvoie ses infos, ex.
    '/Game/Mods/DevKitApiTest/MonBlueprint.MonBlueprint' ou '/Game/PrimalEarth/Dinos/Dodo/Dodo_Character_BP.Dodo_Character_BP'."""
    # la DLL attend jusqu'a timeout_ms (defaut 600 s pour load_asset) ; le socket attend un peu plus
    return _send({"cmd": "load_asset", "path": path, "timeout_ms": 600000}, timeout=620.0)


@mcp.tool()
def devkit_get_property(object_path: str, property_name: str, index: int = 0, cdo: bool = True) -> str:
    """Lit une propriete UPROPERTY par reflexion (UProperty::ExportText_InContainer). object_path = asset ou classe ;
    cdo=true (defaut) : si c'est une UClass ou un Blueprint, on lit sur le Class Default Object (les 'defaults').
    Renvoie value (texte au format ImportText/ExportText), type, array_dim, offset, size."""
    return _send({"cmd": "get_property", "object": object_path, "property": property_name, "index": index, "cdo": cdo})


@mcp.tool()
def devkit_set_property(object_path: str, property_name: str, value: str, index: int = 0, cdo: bool = True) -> str:
    """Ecrit une propriete UPROPERTY (UProperty::ImportText) puis PostEditChange + MarkPackageDirty. value au format texte UE
    (ex. '3.5', 'True', 'Mon texte', '(X=1,Y=2,Z=3)'). Ne sauvegarde pas : appeler devkit_save_package ensuite. Renvoie la valeur relue."""
    return _send({"cmd": "set_property", "object": object_path, "property": property_name, "value": value, "index": index, "cdo": cdo})


@mcp.tool()
def devkit_save_package(package_or_asset: str) -> str:
    """Sauvegarde un package sur disque (UEditorEngine::SavePackage). Accepte un chemin de package '/Game/Mods/DevKitApiTest/X'
    ou d'asset '/Game/Mods/DevKitApiTest/X.X'. LISTE BLANCHE : seuls les packages sous /Game/Mods/DevKitApiTest/ sont acceptes (verifie dans la DLL)."""
    req = {"cmd": "save_package"}
    req["asset" if "." in package_or_asset.rsplit("/", 1)[-1] else "package"] = package_or_asset
    return _send(req, timeout=300.0)


@mcp.tool()
def devkit_duplicate_asset(source_path: str, dest_package: str, dest_name: str = "") -> str:
    """Duplique un asset (StaticDuplicateObject) dans un nouveau package sous /Game/Mods/DevKitApiTest/ (liste blanche DLL),
    notifie l'Asset Registry et marque le package dirty. dest_name defaut = dernier segment de dest_package. Ne sauvegarde pas."""
    req = {"cmd": "duplicate_asset", "source": source_path, "dest_package": dest_package}
    if dest_name:
        req["dest_name"] = dest_name
    return _send(req, timeout=600.0)


@mcp.tool()
def devkit_create_blueprint(parent_class: str, package: str, name: str = "") -> str:
    """Cree un Blueprint (FKismetEditorUtilities::CreateBlueprint) derive de parent_class (ex. '/Script/Engine.Actor' ou
    '/Game/.../X.X_C') dans package sous /Game/Mods/DevKitApiTest/ (liste blanche DLL). Notifie l'Asset Registry + dirty. Ne sauvegarde pas."""
    req = {"cmd": "create_blueprint", "parent_class": parent_class, "package": package}
    if name:
        req["name"] = name
    return _send(req, timeout=300.0)


@mcp.tool()
def devkit_compile_blueprint(blueprint_path: str) -> str:
    """Compile un Blueprint (FKismetEditorUtilities::CompileBlueprint) et renvoie son statut (UpToDate / Error /
    UpToDateWithWarnings...), errors, warnings et messages [{severity, text}] = les lignes LogBlueprint '[compiler X] ...'
    du compilateur Kismet capturees pendant la compilation (chaque message cite le noeud fautif)."""
    return _send({"cmd": "compile_blueprint", "blueprint": blueprint_path}, timeout=300.0)


# ---------------------------------------------------------------------------------------------------------
# Phase 4 ECRITURE (DLL : add_variable / remove_variable / add_function / remove_graph / paste_t3d / link_pins / list_nodes)
# Tout est refuse par la DLL hors /Game/Mods/DevKitApiTest/ (liste blanche sur le package reel du Blueprint).
# ---------------------------------------------------------------------------------------------------------
def _pin_type_args(type_str: str, prefix: str = "") -> dict:
    d = t3d_write.parse_type(type_str)
    out = {prefix + "category": d["category"], prefix + "is_array": d["is_array"], prefix + "is_reference": d["is_reference"]}
    if d.get("sub_object"):
        out[prefix + "sub_object"] = d["sub_object"]
    if d.get("sub_category"):
        out[prefix + "sub_category"] = d["sub_category"]
    return out


def _parse_pin_list(s: str) -> list:
    """'Count:int, Target:object:/Script/Engine.Actor, Items:object:/Script/Engine.Actor[]' -> [(name, type)]"""
    res = []
    for part in [p.strip() for p in (s or "").split(",") if p.strip()]:
        name, _, typ = part.partition(":")
        if not typ:
            raise ValueError("pin sans type : " + part)
        res.append((name.strip(), typ.strip()))
    return res


@mcp.tool()
def devkit_add_variable(blueprint_path: str, name: str, type: str = "int", default: str = "", replicated: bool = False,
                        rep_notify: str = "", category: str = "", rep_condition: int = -1) -> str:
    """ECRITURE. Ajoute une variable membre a un Blueprint (FBlueprintEditorUtils::AddMemberVariable, FEdGraphPinType construit par
    reflexion). type : 'int' 'float' 'bool' 'string' 'name' 'text' 'byte' 'double' | 'object:/Script/Engine.Actor' | 'class:/Script/...'
    | 'struct:/Script/Engine.Actor:BPNetExecParams' | suffixe '[]' = tableau. replicated=true -> CPF_Net ; rep_notify='OnRep_X' ->
    RepNotify + CPF_Net ; category = categorie d'affichage ; rep_condition = ELifetimeCondition (-1 = inchange).
    Ne sauvegarde pas (save_package ensuite). Liste blanche /Game/Mods/DevKitApiTest/."""
    req = {"cmd": "add_variable", "blueprint": blueprint_path, "name": name, "replicated": replicated}
    req.update(_pin_type_args(type))
    if default != "":
        req["default"] = default
    if rep_notify:
        req["rep_notify"] = rep_notify
    if category:
        req["var_category"] = category
    if rep_condition >= 0:
        req["rep_condition"] = rep_condition
    return _send(req, timeout=300.0)


@mcp.tool()
def devkit_remove_variable(blueprint_path: str, name: str) -> str:
    """ECRITURE. Supprime une variable membre (FBlueprintEditorUtils::RemoveMemberVariable). Liste blanche DLL."""
    return _send({"cmd": "remove_variable", "blueprint": blueprint_path, "name": name}, timeout=300.0)


@mcp.tool()
def devkit_add_function(blueprint_path: str, name: str, inputs: str = "", outputs: str = "", override: bool = False) -> str:
    """ECRITURE. Cree un graph de fonction (CreateNewGraph + equivalent d'AddFunctionGraph : entree + sortie, flags
    BlueprintCallable|BlueprintEvent|Public, entree editable). inputs/outputs = 'Nom:type, Nom2:type' (types comme add_variable).
    override=true : name = fonction de la classe parente a surcharger (ex. BPServerHandleNetExecCommand) -> entree/sortie
    generees depuis la UFunction (inputs/outputs ignores). Renvoie graph, entry_node, result_node (noms d'objets a utiliser
    dans les liens '@<nom>' de devkit_write_graph / devkit_link_pins). Liste blanche DLL."""
    req = {"cmd": "add_function", "blueprint": blueprint_path, "name": name, "override": override}
    for i, (pn, pt) in enumerate(_parse_pin_list(inputs)):
        req["in%d_name" % i] = pn
        req.update(_pin_type_args(pt, "in%d_" % i))
    for i, (pn, pt) in enumerate(_parse_pin_list(outputs)):
        req["out%d_name" % i] = pn
        req.update(_pin_type_args(pt, "out%d_" % i))
    return _send(req, timeout=300.0)


@mcp.tool()
def devkit_remove_graph(blueprint_path: str, graph: str) -> str:
    """ECRITURE. Supprime un graph de fonction/macro (FBlueprintEditorUtils::RemoveGraph). Refuse les pages d'ubergraph. Liste blanche DLL."""
    return _send({"cmd": "remove_graph", "blueprint": blueprint_path, "graph": graph}, timeout=300.0)


@mcp.tool()
def devkit_list_nodes(blueprint_path: str, graph: str) -> str:
    """LECTURE. Liste les noeuds d'un graph (nom d'objet, classe, position, pins : nom, sens, type, nb de liens, defaut).
    graph = nom ('EventGraph', 'MaFonction') ou chemin objet complet. Plus leger qu'un export T3D ; sert a verifier un paste."""
    return _send({"cmd": "list_nodes", "blueprint": blueprint_path, "graph": graph}, timeout=120.0)


@mcp.tool()
def devkit_paste_t3d(blueprint_path: str, graph: str, t3d: str, reconstruct: bool = True, offset_x: int = 0, offset_y: int = 0) -> str:
    """ECRITURE. Colle du T3D 'presse-papiers' (blocs 'Begin Object Class=K2Node_... Name=...' au niveau 0, pins EdGraphPin imbriquees,
    format de FEdGraphUtilities::ExportNodesToText) dans un graph : ImportNodesFromText + CreateNewGuid + PostPasteNode
    (+ ReconstructNode sur chaque K2Node si reconstruct=true : le moteur regenere les pins depuis la fonction/variable/struct en
    gardant liens et defauts par nom). Les liens vers des noeuds absents du T3D sont casses par le moteur -> devkit_link_pins.
    Renvoie les noeuds crees (nom d'objet, classe, nb de pins) et le journal d'import. Liste blanche DLL."""
    return _send({"cmd": "paste_t3d", "blueprint": blueprint_path, "graph": graph, "t3d": t3d, "reconstruct": reconstruct,
                  "offset_x": offset_x, "offset_y": offset_y}, timeout=300.0)


@mcp.tool()
def devkit_link_pins(blueprint_path: str, graph: str, from_node: str, from_pin: str, to_node: str, to_pin: str, force: bool = False) -> str:
    """ECRITURE. Relie deux pins par NOM D'OBJET de noeud (ex. 'K2Node_FunctionEntry_0', ou le nom renvoye par paste/write_graph)
    et NOM DE PIN ('then', 'execute', 'ReturnValue'...). UEdGraphSchema_K2::TryCreateConnection (valide les types, remplace un lien
    exec existant) ; force=true = UEdGraphPin::MakeLinkTo sans validation. Liste blanche DLL."""
    return _send({"cmd": "link_pins", "blueprint": blueprint_path, "graph": graph, "from": from_node, "from_pin": from_pin,
                  "to": to_node, "to_pin": to_pin, "force": force}, timeout=120.0)


@mcp.tool()
def devkit_set_pin_default(blueprint_path: str, graph: str, node: str, pin: str, value: str = "", object_path: str = "") -> str:
    """ECRITURE. Valeur par defaut d'une pin d'un noeud EXISTANT (ex. ReturnValue du K2Node_FunctionResult cree par add_function) :
    value (texte, UEdGraphSchema_K2::TrySetDefaultValue) ou object_path (pin objet/classe, TrySetDefaultObject). Liste blanche DLL."""
    req = {"cmd": "set_pin_default", "blueprint": blueprint_path, "graph": graph, "node": node, "pin": pin}
    if object_path:
        req["object"] = object_path
    else:
        req["value"] = value
    return _send(req, timeout=120.0)


@mcp.tool()
def devkit_pie(action: str = "start", at_player_start: bool = True) -> str:
    """PIE. action='start' : demande une session Play In Editor sur la map ouverte (UEditorEngine::RequestPlaySession, nouvelle
    fenetre, demarree au tick suivant) ; 'stop' : RequestEndPlayMap ; 'world' : renvoie le chemin du monde PIE courant
    (obj list class=World, nom UEDPIE_*) a passer a devkit_exec(world_path) / devkit_console_command."""
    if action == "world":
        out = _exec("obj list class=World")
        worlds = re.findall(r"World (\S*UEDPIE_\d+_\S+)", out)
        return json.dumps({"ok": bool(worlds), "pie_worlds": worlds})
    return _send({"cmd": "pie_start" if action == "start" else "pie_stop", "at_player_start": at_player_start}, timeout=60.0)


@mcp.tool()
def devkit_console_command(world_path: str, command: str) -> str:
    """PIE. Execute une commande console COMME LE JOUEUR (APlayerController::ConsoleCommand du 1er PlayerController du monde donne,
    prefixe 'cheat' d'ARK compris), ex. world_path=<monde PIE> command='cheat summon BP_X_C'. Renvoie la sortie."""
    return _send({"cmd": "console_command", "world_path": world_path, "command": command}, timeout=120.0)


@mcp.tool()
def devkit_write_graph(blueprint_path: str, graph: str, spec_json: str, reconstruct: bool = True) -> str:
    """ECRITURE. Ecrit un lot de noeuds + liens dans un graph a partir d'une spec JSON compacte (voir tools/t3d_write.py) :
    {"nodes":[{"id":"ev","type":"custom_event","name":"Ping","params":[["Count","int"]],"net":"server"},
              {"id":"c","type":"call","class":"/Script/Engine.KismetSystemLibrary","function":"PrintString","static":true,
               "defaults":{"InString":"hi"},"pos":[400,0]}, {"id":"g","type":"var_get","name":"N","vtype":"int"},
              {"id":"s","type":"var_set",...}, {"id":"b","type":"branch"}, {"id":"me","type":"self"},
              {"id":"k","type":"cast","target":"/Script/Engine.PlayerController"},
              {"id":"m","type":"make_struct","struct":"/Script/Engine.Actor:BPNetExecParams","fields":[["IntParam1","int"]]},
              {"id":"e","type":"event","class":"/Script/Engine.Actor","function":"ReceiveBeginPlay"}, {"id":"q","type":"sequence","outputs":2}],
     "links":[["ev","then","c","execute"], ["@K2Node_FunctionEntry_0","then","b","execute"], ["b","then","@K2Node_FunctionResult_0","execute"]]}
    Genere le T3D (t3d_write.GraphBuilder), le colle (paste_t3d), puis pose les liens '@<NomObjet>' vers les noeuds deja
    presents (entree/sortie de fonction) par link_pins. Renvoie ids -> noms d'objets, resultat du paste et des liens externes."""
    try:
        g = t3d_write.GraphBuilder.from_spec(spec_json)
        text = g.t3d()
    except (ValueError, KeyError, json.JSONDecodeError) as e:
        return json.dumps({"ok": False, "error": "spec invalide : %s" % e})
    names = g.obj_names()
    raw = _send({"cmd": "paste_t3d", "blueprint": blueprint_path, "graph": graph, "t3d": text, "reconstruct": reconstruct}, timeout=300.0)
    try:
        pasted = json.loads(raw)
    except json.JSONDecodeError:
        return json.dumps({"ok": False, "error": "reponse paste illisible : " + raw[:300]})
    result = {"ok": bool(pasted.get("ok")), "names": names, "t3d_chars": len(text), "paste": pasted.get("result") or pasted.get("error"), "links": []}
    if not pasted.get("ok"):
        return json.dumps(result, ensure_ascii=False)
    for a, apin, b, bpin in g.external_links:
        na = a[1:] if a.startswith("@") else names.get(a, a)
        nb = b[1:] if b.startswith("@") else names.get(b, b)
        r = _send({"cmd": "link_pins", "blueprint": blueprint_path, "graph": graph, "from": na, "from_pin": apin, "to": nb, "to_pin": bpin}, timeout=120.0)
        try:
            rj = json.loads(r)
        except json.JSONDecodeError:
            rj = {"ok": False, "error": r[:200]}
        result["links"].append({"from": "%s.%s" % (na, apin), "to": "%s.%s" % (nb, bpin), "ok": rj.get("ok"), "detail": rj.get("result") or rj.get("error")})
        result["ok"] = result["ok"] and bool(rj.get("ok"))
    return json.dumps(result, ensure_ascii=False)


@mcp.tool()
def devkit_add_component(blueprint_path: str, component_class: str, name: str) -> str:
    """Ajoute un composant au SimpleConstructionScript d'un Blueprint d'acteur (USimpleConstructionScript::CreateNode + AddNode,
    puis MarkBlueprintAsStructurallyModified). component_class ex. '/Script/Engine.StaticMeshComponent'. Compiler ensuite."""
    return _send({"cmd": "add_component", "blueprint": blueprint_path, "component_class": component_class, "name": name}, timeout=300.0)


# ---------------------------------------------------------------------------------------------------------
# Lecture seule cote Python (aucune commande DLL nouvelle) : obj export T3D via exec -> t3d_graph.py
# ---------------------------------------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent
CACHE_DIR = Path(os.environ.get("DEVKITAPI_CACHE") or (REPO_ROOT / "cache" / "t3d"))
OUT_ROOT = Path(os.environ.get("DEVKITAPI_OUT_ROOT") or REPO_ROOT)   # seule racine autorisee pour out_file
DEFAULT_MAX_INLINE = 60000


class ReadError(Exception):
    pass


def _exec(command: str, timeout: float = 300.0) -> str:
    """exec DLL sans echo dans l'Output Log ; renvoie la sortie capturee, leve ReadError si la DLL dit non"""
    raw = _send({"cmd": "exec", "command": command, "to_log": False}, timeout=timeout)
    try:
        r = json.loads(raw)
    except json.JSONDecodeError:
        raise ReadError("reponse DLL illisible : " + raw[:300])
    if not r.get("ok"):
        raise ReadError(r.get("error", raw[:300]))
    return (r.get("result") or {}).get("output", "")


def _safe_name(object_path: str) -> str:
    """nom de fichier court et unique : 8 car. de hash + fin du chemin (limite MAX_PATH Windows = 260 car. au total)"""
    import hashlib
    h = hashlib.md5(object_path.encode("utf-8")).hexdigest()[:8]
    tail = re.sub(r"[^A-Za-z0-9_.-]+", "_", object_path.strip("/").split("/")[-1])[-70:]
    return h + "_" + tail


def _export(object_path: str, tag: str = "") -> Path:
    """obj export type=Object -> fichier T3D dans le cache (ecrit par l'editeur). Leve ReadError si rien n'est produit."""
    d = CACHE_DIR / (tag or "misc")
    d.mkdir(parents=True, exist_ok=True)
    f = d / (_safe_name(object_path) + ".t3d")
    if f.exists():
        f.unlink()
    out = _exec("obj export type=Object name=%s file=%s" % (object_path, f))
    if not f.exists():
        raise ReadError("obj export n'a rien produit pour %s (objet non charge ? chemin faux ?) : %s"
                        % (object_path, out.strip()[:300] or "aucune sortie"))
    return f


def _objdump(object_path: str) -> dict:
    """obj dump -> {prop: valeur | [valeurs]} (tronque par l'editeur a 100 elements/tableau et 64 Ko : usage = en-tetes)"""
    out = _exec("obj dump " + object_path)
    if "Property dump for object" not in out:
        raise ReadError("obj dump a echoue pour %s : %s" % (object_path, out.strip()[:300]))
    props = {}
    for line in out.splitlines():
        m = re.match(r"^\s{2}([A-Za-z_]\w*)(?:\((\d+)\))?=(.*)$", line)
        if not m:
            continue
        k, idx, v = m.group(1), m.group(2), m.group(3)
        if idx is not None:
            props.setdefault(k, []).append(v)
        else:
            props[k] = v
    return props


LOG_FILE = Path(os.environ.get("ARK_DEVKIT_ROOT") or r"G:\DEVKIT\ARKEditor") / "Projects" / "ShooterGame" / "Saved" / "Logs" / "ShooterGame.log"
RE_LOGPREFIX = re.compile(r"^\[[^\]]*\]\[\s*\d+\]LogDevKitApi:\s?")


def _objdump_full(object_path: str) -> dict:
    """obj dump COMPLET : la reponse DLL est coupee a 64 Ko, l'Output Log ne l'est pas -> on encadre le dump de deux
    marqueurs (devkit_log), on force FLUSHLOG, puis on relit ShooterGame.log. Les tableaux > 100 elements restent coupes
    par l'editeur lui-meme ('... N more elements') : signales dans truncated_arrays. Repli = reponse DLL si le log
    n'est pas lisible."""
    tag = "%d_%d" % (int(time.time() * 1000), next(_ids))
    _send({"cmd": "log", "text": "READOBJ BEGIN " + tag})
    resp = _send({"cmd": "exec", "command": "obj dump " + object_path, "to_log": True}, timeout=300.0)
    _send({"cmd": "log", "text": "READOBJ END " + tag})
    _send({"cmd": "exec", "command": "FLUSHLOG", "to_log": False})
    text, source = None, "log"
    for _ in range(120):                       # jusqu'a 30 s : le flush du log peut tarder quand l'editeur est charge
        if LOG_FILE.exists():
            with open(LOG_FILE, "rb") as f:    # on ne lit que la fin du fichier (le log grossit de plusieurs Mo/session)
                f.seek(0, os.SEEK_END)
                size = f.tell()
                f.seek(max(0, size - 8_000_000))
                data = f.read().decode("utf-8", "replace")
            b, e = data.rfind("READOBJ BEGIN " + tag), data.rfind("READOBJ END " + tag)
            if b != -1 and e > b:
                text = data[b:e]
                break
        time.sleep(0.25)
    if text is None:
        try:
            r = json.loads(resp)
        except json.JSONDecodeError:
            raise ReadError("obj dump : reponse illisible pour " + object_path)
        if not r.get("ok"):
            raise ReadError("obj dump a echoue pour %s : %s" % (object_path, r.get("error", "")))
        text, source = (r.get("result") or {}).get("output", ""), "response(tronquee 64 Ko)"
    if "Property dump for object" not in text:
        raise ReadError("obj dump a echoue pour %s : %s" % (object_path, text.strip()[-300:]))
    props, trunc, last = {}, [], None
    for line in text.splitlines():
        line = RE_LOGPREFIX.sub("", line)
        m = re.match(r"^\s*([A-Za-z_]\w*)(?:\((\d+)\))?=(.*)$", line)
        if m:
            k, idx, v = m.group(1), m.group(2), m.group(3)
            if idx is not None:
                props.setdefault(k, {})[int(idx)] = v
            else:
                props[k] = v
            last = k
            continue
        m = re.match(r"^\s*\.\.\.\s*(\d+) more elements", line)
        if m and last:
            trunc.append("%s (+%s)" % (last, m.group(1)))
    return {"props": props, "truncated_arrays": trunc, "source": source}


def _merged_obj(object_path: str, tag: str):
    """T3D (tableaux complets mais certaines proprietes non exportees : transient/config...) + obj dump complet
    (toutes les proprietes, tableaux coupes a 100). Le T3D gagne quand il a la propriete."""
    f = _export(object_path, tag)
    obj = t3d_graph.first(t3d_graph.parse(f))
    dump = _objdump_full(object_path)
    added = 0
    for k, v in dump["props"].items():
        if k not in obj.props:
            obj.props[k] = v
            added += 1
    t3d_arrays = {k for k, v in t3d_graph.first(t3d_graph.parse(f)).props.items() if isinstance(v, dict)}
    # tableaux coupes a 100 par obj dump ET absents du T3D -> completes par get_property (ExportText du tableau entier)
    completed, still_truncated = [], []
    for t in dump["truncated_arrays"]:
        k = t.split(" ")[0]
        if k in t3d_arrays:
            continue
        try:
            r = json.loads(_send({"cmd": "get_property", "object": object_path, "property": k, "index": 0, "cdo": False}, timeout=120.0))
            val = (r.get("result") or {}).get("value", "") if r.get("ok") else ""
        except json.JSONDecodeError:
            val = ""
        if val.startswith("(") and val.endswith(")"):
            elems = _split_top(val[1:-1])
            obj.props[k] = {i: e for i, e in enumerate(elems)}
            completed.append("%s (%d)" % (k, len(elems)))
        else:
            still_truncated.append(t)
    info = {"t3d_file": str(f), "dump_source": dump["source"], "props_from_dump_only": added,
            "arrays_completed_by_get_property": completed, "truncated_arrays": still_truncated}
    return obj, info


def _split_top(s: str):
    """decoupe 'a,b,(c,d),"e,f"' sur les virgules de niveau 0 (parentheses et guillemets respectes)"""
    out, cur, depth, inq = [], "", 0, False
    for ch in s:
        if ch == '"':
            inq = not inq
        if not inq:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                out.append(cur)
                cur = ""
                continue
        cur += ch
    if cur or out:
        out.append(cur)
    return out


def _split_asset(path: str):
    """'/Game/A/B.B' | '/Game/A/B' | '/Game/A/B.B_C' -> (package, nom, chemin asset)"""
    path = path.strip()
    if path.startswith("Blueprint'") or path.startswith("BlueprintGeneratedClass'"):
        path = path.split("'", 1)[1].rstrip("'")
    pkg, _, obj = path.rpartition(".")
    if not pkg or "/" in obj:
        pkg, obj = path, path.rsplit("/", 1)[-1]
    if obj.endswith("_C") and obj[:-2] == pkg.rsplit("/", 1)[-1]:
        obj = obj[:-2]
    return pkg, obj, "%s.%s" % (pkg, obj)


def _ref_path(value: str) -> str:
    """Class'/Script/X.Y' | BlueprintGeneratedClass'"/Game/A.B_C"' -> /Script/X.Y"""
    m = re.search(r"'\"?([^'\"]+)\"?'", value or "")
    return m.group(1) if m else ""


def _parent_cdo(parent_class_value: str) -> str:
    p = _ref_path(parent_class_value)
    if not p:
        return ""
    pkg, _, cls = p.rpartition(".")
    return "%s.Default__%s" % (pkg, cls)


def _finish(result: dict, out_file: str, max_inline: int, stem: str) -> str:
    """renvoie le JSON inline s'il est petit, sinon l'ecrit (out_file sous OUT_ROOT = le depot, ou cache) et renvoie un resume"""
    text = json.dumps(result, ensure_ascii=False, indent=1)
    target = None
    if out_file:
        target = Path(out_file)
        try:
            target.resolve().relative_to(OUT_ROOT.resolve())
        except ValueError:
            return json.dumps({"ok": False, "error": "out_file doit etre sous %s" % OUT_ROOT})
    elif len(text) > max_inline:
        target = CACHE_DIR / "json" / ("%s_%d.json" % (stem, int(time.time())))
    if target is None:
        return text
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8")
    summary = {k: v for k, v in result.items() if k in ("ok", "object", "blueprint", "parent", "summary")}
    summary.update({"written": str(target), "bytes": len(text),
                    "note": "resultat complet ecrit dans le fichier (trop gros pour une reponse inline ou out_file demande)"})
    return json.dumps(summary, ensure_ascii=False, indent=1)


@mcp.tool()
def devkit_read_object(path: str, diff_against: str = "", out_file: str = "", max_inline: int = DEFAULT_MAX_INLINE) -> str:
    """LECTURE SEULE. Exporte un objet charge (CDO, composant, graph, SCS...) en T3D complet via 'obj export type=Object'
    (aucune troncature, contrairement a 'obj dump' : 100 elements/tableau, 64 Ko) et le renvoie en JSON
    {class, name, props, subobjects}. path = chemin objet complet, ex. '/Game/Mods/X/BP.Default__BP_C' (CDO) ou
    '/Game/Mods/X/BP.BP_C:SimpleConstructionScript_0'. diff_against = 2e objet (ex. CDO du parent) -> ajoute
    diff {only_a (enfant seul), only_b (parent seul), changed {a,b}} sur les proprietes aplaties 'Sous/Prop(i)'.
    Gros resultats : ecrits dans out_file (sous OUT_ROOT = le depot) ou dans le cache, reponse = resume + chemin.
    N'ecrit jamais dans les assets ; le seul fichier produit est le T3D/JSON sous <repo>/cache."""
    try:
        obj, info = _merged_obj(path, "objects")
        result = {"ok": True, "object": path, "sources": info, "data": t3d_graph.obj_to_dict(obj)}
        if diff_against:
            obj2, info2 = _merged_obj(diff_against, "objects")
            d = t3d_graph.diff(obj, obj2)
            result["diff_against"] = diff_against
            result["diff_sources"] = info2
            result["diff"] = d
            result["summary"] = {"only_a": len(d["only_a"]), "only_b": len(d["only_b"]), "changed": len(d["changed"])}
        return _finish(result, out_file, max_inline, _safe_name(path))
    except ReadError as e:
        return json.dumps({"ok": False, "error": str(e)})


@mcp.tool()
def devkit_read_blueprint(path: str, graph: str = "", format: str = "pseudo", include_cdo: bool = True,
                          include_components: bool = True, out_file: str = "", max_inline: int = DEFAULT_MAX_INLINE) -> str:
    """LECTURE SEULE. Reconstitue un Blueprint complet sans toucher a l'asset : parent, variables (NewVariables),
    composants du SimpleConstructionScript (+ proprietes surchargees des templates), surcharges du CDO par rapport au
    CDO du parent, et TOUS les graphs (Ubergraph/Function/Macro/Delegate) exportes en T3D via 'obj export' puis
    decodes par t3d_graph.py : noeuds, pins, liens, valeurs par defaut.
    path = '/Game/Mods/X/BP.BP' (ou '/Game/Mods/X/BP', ou la classe '..._C'). L'asset est charge si besoin (load_asset).
    graph = ne sortir qu'un graph (nom exact, ex. 'ProcessMessage'). format = 'pseudo' (pseudo-code texte, defaut)
    | 'json' (noeuds/pins/liens) | 'both'. Gros resultats (ex. 400 noeuds) : ecrits dans out_file (sous OUT_ROOT = le depot)
    ou dans le cache, reponse = resume + chemin du fichier a lire ensuite."""
    fmt = format.lower().strip()
    if fmt not in ("pseudo", "json", "both"):
        return json.dumps({"ok": False, "error": "format doit etre pseudo | json | both"})
    try:
        pkg, name, asset = _split_asset(path)
        loaded = json.loads(_send({"cmd": "load_asset", "path": asset, "timeout_ms": 600000}, timeout=620.0))
        if not loaded.get("ok") or not (loaded.get("result") or {}).get("found"):
            raise ReadError("asset introuvable : %s (%s)" % (asset, loaded.get("error", "")))
        bp = _objdump(asset)
        parent_class = bp.get("ParentClass", "")
        gen_class = _ref_path(bp.get("GeneratedClass", "")) or "%s.%s_C" % (pkg, name)
        result = {"ok": True, "blueprint": asset, "class": (loaded.get("result") or {}).get("class"),
                  "parent": _ref_path(parent_class), "generated_class": gen_class,
                  "status": bp.get("Status", ""), "variables": [], "graphs": [], "components": [], "notes": []}

        # --- variables ---
        for v in bp.get("NewVariables", []) if isinstance(bp.get("NewVariables"), list) else []:
            s = t3d_graph.parse_struct(v)
            vt = t3d_graph.parse_struct(s.get("VarType", "()"))
            result["variables"].append({
                "name": t3d_graph.unq(s.get("VarName", "?")),
                "category": t3d_graph.unq(vt.get("PinCategory", "")),
                "sub": t3d_graph.short(vt.get("PinSubCategoryObject", "") or vt.get("PinSubCategory", "")).strip('"'),
                "array": vt.get("bIsArray") == "True",
                "default": t3d_graph.unq(s.get("DefaultValue", "")),
                "editable_category": t3d_graph.unq(s.get("Category", "")),
                "rep_condition": s.get("RepCondition", ""),
            })
        if len(bp.get("NewVariables", []) if isinstance(bp.get("NewVariables"), list) else []) >= 100:
            result["notes"].append("NewVariables tronque a 100 par obj dump")

        # --- graphs ---
        kinds = (("UbergraphPages", "ubergraph"), ("FunctionGraphs", "function"), ("MacroGraphs", "macro"),
                 ("DelegateSignatureGraphs", "delegate"))
        wanted = []
        for key, kind in kinds:
            vals = bp.get(key, [])
            for v in (vals if isinstance(vals, list) else [vals]):
                gp = _ref_path(v)
                if gp:
                    wanted.append((kind, gp))
        for key in ("EventGraphs", "IntermediateGeneratedGraphs"):
            vals = bp.get(key, [])
            names = [_ref_path(v).split(":")[-1] for v in (vals if isinstance(vals, list) else [vals]) if _ref_path(v)]
            if names:
                result["notes"].append("%s (sous-graphs listes, contenus dans l'ubergraph) : %s" % (key, ", ".join(names)))
        if graph:
            wanted = [(k, gp) for k, gp in wanted if gp.split(":")[-1] == graph]
            if not wanted:
                raise ReadError("graph '%s' introuvable ; graphs : %s" % (graph, ", ".join(gp.split(":")[-1] for _, gp in wanted)))
        for kind, gp in wanted:
            f = _export(gp, "graphs/" + _safe_name(asset))
            g = t3d_graph.Graph(t3d_graph.parse(f))
            entry = {"name": g.name, "kind": kind, "node_count": len(g.nodes), "t3d_file": str(f)}
            broken = g.broken_links()
            if broken:
                entry["broken_links"] = broken
            if fmt in ("pseudo", "both"):
                entry["pseudo"] = t3d_graph.pseudo(g)
            if fmt in ("json", "both"):
                entry["nodes"] = t3d_graph.graph_to_dict(g)["nodes"]
            result["graphs"].append(entry)

        # --- composants (SCS) ---
        scs = _ref_path(bp.get("SimpleConstructionScript", ""))
        if include_components and scs:
            f = _export(scs, "scs")
            scs_obj = t3d_graph.first(t3d_graph.parse(f))
            nodes = {}
            for cn, co in scs_obj.children.items():
                if not cn.startswith("SCS_Node"):
                    continue
                tpl = _ref_path(co.get("ComponentTemplate", ""))
                nodes[cn] = {"scs_node": cn, "variable": t3d_graph.unq(co.get("VariableName", "")),
                             "class": (co.get("ComponentTemplate", "").split("'", 1)[0]),
                             "template": ("%s.%s" % (pkg, tpl) if tpl and not tpl.startswith("/") else tpl),
                             "parent_scs_node": None,
                             "children": [c.split("'")[-2].strip('"').split(".")[-1] for c in co.arr("ChildNodes")]}
            for n in nodes.values():
                for ch in n["children"]:
                    if ch in nodes:
                        nodes[ch]["parent_scs_node"] = n["scs_node"]
            roots = [c.split("'")[-2].strip('"').split(".")[-1] for c in scs_obj.arr("RootNodes")]
            for n in nodes.values():
                n["root"] = n["scs_node"] in roots
                if n["template"]:
                    try:
                        ft = _export(n["template"], "templates")
                        n["template_props"] = t3d_graph.obj_to_dict(t3d_graph.first(t3d_graph.parse(ft)))["props"]
                    except ReadError as e:
                        n["template_props"] = {"_error": str(e)}
            result["components"] = list(nodes.values())

        # --- surcharges CDO vs parent ---
        if include_cdo:
            cdo = "%s.Default__%s_C" % (pkg, name)
            pcdo = _parent_cdo(parent_class)
            try:
                cobj, cinfo = _merged_obj(cdo, "cdo")
                if pcdo:
                    pobj, pinfo = _merged_obj(pcdo, "cdo")
                    d = t3d_graph.diff(cobj, pobj)
                    result["cdo_overrides"] = {"cdo": cdo, "parent_cdo": pcdo, "changed": d["changed"],
                                               "only_child": d["only_a"], "only_parent": d["only_b"],
                                               "sources": {"child": cinfo, "parent": pinfo}}
                else:
                    result["cdo_overrides"] = {"cdo": cdo, "props": t3d_graph.obj_to_dict(cobj)["props"], "sources": cinfo}
            except ReadError as e:
                result["cdo_overrides"] = {"_error": str(e)}
            result["notes"].append("cdo_overrides = diff enfant/parent sur T3D (tableaux complets) fusionne avec obj dump "
                                   "complet relu dans l'Output Log (toutes les proprietes) ; only_parent = valeurs presentes "
                                   "chez le parent seulement (souvent heritees non re-exportees, a lire avec prudence)")

        result["summary"] = {"variables": len(result["variables"]),
                             "graphs": {g["name"]: g["node_count"] for g in result["graphs"]},
                             "components": [c["variable"] for c in result["components"]],
                             "cdo_changed": len((result.get("cdo_overrides") or {}).get("changed", {}) or {}),
                             "cdo_only_child": len((result.get("cdo_overrides") or {}).get("only_child", {}) or {})}
        return _finish(result, out_file, max_inline, _safe_name(asset))
    except ReadError as e:
        return json.dumps({"ok": False, "error": str(e)})


@mcp.tool()
def devkit_raw(request_json: str) -> str:
    """Envoie une requete JSON brute a la DLL (debug), ex. '{"cmd":"ping"}'. L'id est ajoute automatiquement s'il manque."""
    try:
        payload = json.loads(request_json)
    except json.JSONDecodeError as e:
        return json.dumps({"ok": False, "error": f"JSON invalide : {e}"})
    return _send(payload, timeout=300.0)


if __name__ == "__main__":
    mcp.run(transport="stdio")
