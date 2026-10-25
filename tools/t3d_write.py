#!/usr/bin/env python3
r"""t3d_write.py - genere le texte T3D "presse-papiers" d'un lot de noeuds Blueprint (format de FEdGraphUtilities::ExportNodesToText,
UE 4.5.1 Wildcard : les pins sont des UObjects EdGraphPin imbriques dans le noeud), a coller par la commande DLL `paste_t3d`
(FEdGraphUtilities::ImportNodesFromText + CreateNewGuid + PostPasteNode + ReconstructNode).

Principe : on n'ecrit que les pins qu'on LIE ou auxquelles on donne une VALEUR PAR DEFAUT ; apres le paste, la DLL appelle
ReconstructNode() sur chaque K2Node -> le moteur regenere la liste complete des pins depuis la UFunction / la variable / la struct
(types exacts, pins cachees, valeurs par defaut natives) en conservant, par NOM de pin, nos liens et nos defauts
(UK2Node::RewireOldPinsToNewPins / CopyPersistentDataFromOldPin). Les types de pin que nous ecrivons sont donc indicatifs.

Liens : LinkedTo(i)=EdGraphPin'"<Noeud>.<Pin>"' resolu depuis la portee du graph. Les references vers un noeud ABSENT du lot
(ex. le K2Node_FunctionEntry cree par add_function) sont CASSEES par PostProcessPastedNodes -> les faire apres coup avec `link_pins`
(ids "@<NomObjetDuNoeud>" dans la spec de write_graph, cf. devkit_mcp.py).

Spec JSON (devkit_write_graph) :
  {"nodes":[ {"id":"ev","type":"custom_event","name":"Ping","params":[["Count","int"]],"net":"server","reliable":true,"pos":[0,0]},
             {"id":"c","type":"call","class":"/Script/Engine.KismetSystemLibrary","function":"PrintString","static":true,
              "pins":[["InString","string","in"]],"defaults":{"InString":"hello"},"pos":[400,0]},
             {"id":"g","type":"var_get","name":"PingCount","vtype":"int"}, {"id":"s","type":"var_set","name":"PingCount","vtype":"int"},
             {"id":"b","type":"branch"}, {"id":"me","type":"self"}, {"id":"k","type":"cast","target":"/Script/Engine.PlayerController"},
             {"id":"m","type":"make_struct","struct":"/Script/Engine.Actor:BPNetExecParams","fields":[["IntParam1","int"]]},
             {"id":"br","type":"break_struct","struct":"...","fields":[["IntParam1","int"]]},
             {"id":"e","type":"event","class":"/Script/Engine.Actor","function":"ReceiveBeginPlay","params":[]},
             {"id":"q","type":"sequence","outputs":2},
             {"id":"aa","type":"call_array","class":"/Script/Engine.KismetArrayLibrary","function":"Array_Add","static":true,
              "pins":[["TargetArray","struct:/Script/CoreUObject.Object:Vector[]&","in"],["NewItem","struct:/Script/CoreUObject.Object:Vector","in"]]} ],
   "links":[ ["ev","then","c","execute"], ["@K2Node_FunctionEntry_0","then","b","execute"] ] }
Types de pin : "exec", "bool", "byte", "int", "float", "double", "string", "name", "text", "wildcard", "self",
  "object:/Script/Engine.Actor", "class:/Script/Engine.Actor", "struct:/Script/Engine.Actor:BPNetExecParams",
  suffixe "[]" = tableau, suffixe "&" = reference (ex. "struct:/Script/Engine.Actor:BPNetExecParams&").
"""
import json
import re
import time

NET_FLAGS = {  # FunctionFlags lus dans les T3D de S+ (FUNC_Net 0x40 | FUNC_NetReliable 0x80 | NetServer 0x200000 | NetClient 0x1000000 | NetMulticast 0x4000)
    ("server", True): 2097344, ("server", False): 2097216,
    ("client", True): 16777408, ("client", False): 16777280,
    ("multicast", True): 16576, ("multicast", False): 16448,
}
SIMPLE = {"exec", "bool", "byte", "int", "float", "double", "string", "name", "text", "wildcard", "delegate", "interface"}


def parse_type(t: str) -> dict:
    """'object:/Script/Engine.Actor[]&' -> {category, sub_object, sub_category, is_array, is_reference}"""
    t = (t or "wildcard").strip()
    is_ref = t.endswith("&")
    if is_ref:
        t = t[:-1]
    is_arr = t.endswith("[]")
    if is_arr:
        t = t[:-2]
    d = {"category": t, "sub_object": "", "sub_category": "", "is_array": is_arr, "is_reference": is_ref}
    if t == "self":
        d.update(category="object", sub_category="self")
    elif ":" in t:
        cat, _, obj = t.partition(":")
        if cat not in ("object", "class", "struct", "interface"):
            raise ValueError("type inconnu : " + t)
        d.update(category=cat, sub_object=obj)
    elif t not in SIMPLE:
        raise ValueError("type inconnu : " + t)
    return d


def _ref_kind(path: str) -> str:
    """prefixe d'une reference T3D : Class'...' pour une classe native, BlueprintGeneratedClass pour un BP, ScriptStruct pour une struct"""
    if path.endswith("_C"):
        return "BlueprintGeneratedClass"
    if "/Script/" in path and ":" in path.split("/")[-1]:
        return "ScriptStruct"
    if "/Game/" in path:
        return "UserDefinedStruct"
    return "Class"


def pin_type_text(d: dict) -> str:
    parts = ['PinCategory="%s"' % d["category"]]
    if d.get("sub_category"):
        parts.append('PinSubCategory="%s"' % d["sub_category"])
    if d.get("sub_object"):
        parts.append("PinSubCategoryObject=%s'%s'" % (_ref_kind(d["sub_object"]), d["sub_object"]))
    if d.get("is_array"):
        parts.append("bIsArray=True")
    if d.get("is_reference"):
        parts.append("bIsReference=True")
    return "(" + ",".join(parts) + ")"


def user_pin_line(name: str, d: dict) -> str:
    """CustomProperties UserDefinedPin ... (format ExportCustomProperties de UK2Node_EditablePinBase, lu tel quel dans les T3D S+)"""
    s = "CustomProperties UserDefinedPin Name=%s IsArray=%d IsReference=%d Category=%s " % (
        name, 1 if d.get("is_array") else 0, 1 if d.get("is_reference") else 0, d["category"])
    if d.get("sub_category"):
        s += "SubCategory=%s " % d["sub_category"]
    if d.get("sub_object"):
        s += "SubCategoryObject=%s " % d["sub_object"]
    return s


def _q(s: str) -> str:
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


class Pin:
    def __init__(self, name, ptype, direction="in", default=None, default_object=None, hidden=False, friendly=None):
        self.name, self.type, self.dir = name, parse_type(ptype), direction
        self.default, self.default_object, self.hidden, self.friendly = default, default_object, hidden, friendly
        self.links = []          # [(node_obj_name, pin_obj_name)]
        self.obj_name = None     # EdGraphPin_<n>


class Node:
    def __init__(self, nid, cls, obj_name, pos=(0, 0)):
        self.id, self.cls, self.obj_name, self.pos = nid, cls, obj_name, pos
        self.pins, self.props, self.custom = [], [], []

    def pin(self, name):
        for p in self.pins:
            if p.name.lower() == name.lower():
                return p
        return None

    def add_pin(self, *a, **k):
        p = Pin(*a, **k)
        self.pins.append(p)
        return p


class GraphBuilder:
    """Construit un lot de noeuds + liens, puis .t3d(). Les noms d'objets sont uniques (prefixe DK_<hex>) pour ne jamais
    entrer en collision avec des noeuds deja presents dans le graph (le factory reutiliserait l'objet existant)."""

    def __init__(self, tag=None):
        self.tag = tag or ("%x" % (int(time.time() * 1000) & 0xFFFFFF))
        self.nodes, self._n, self._p = [], 0, 0
        self.external_links = []   # [(node_id_or_@obj, pin, node_id_or_@obj, pin)] a faire par link_pins apres le paste

    # ---- fabrique -------------------------------------------------------------------------------------------------
    def _new(self, nid, cls, pos):
        self._n += 1
        n = Node(nid, cls, "DK%s_%s_%d" % (self.tag, cls.replace("K2Node_", ""), self._n), tuple(pos or (0, 0)))
        if self.get(nid):
            raise ValueError("id de noeud en double : " + nid)
        self.nodes.append(n)
        return n

    def get(self, nid):
        for n in self.nodes:
            if n.id == nid:
                return n
        return None

    def custom_event(self, nid, name, params=(), net=None, reliable=True, pos=(0, 0)):
        n = self._new(nid, "K2Node_CustomEvent", pos)
        n.add_pin("OutputDelegate", "delegate", "out")
        n.add_pin("then", "exec", "out")
        n.props.append('CustomFunctionName="%s"' % name)
        if net:
            n.props.append("FunctionFlags=%d" % NET_FLAGS[(net, bool(reliable))])
        for pname, ptype in params:
            n.add_pin(pname, ptype, "out")
            n.custom.append(user_pin_line(pname, parse_type(ptype)))
        return n

    def event(self, nid, cls, function, params=(), pos=(0, 0)):
        """surcharge d'un event natif (ReceiveBeginPlay, BPDeactivated...) dans l'ubergraph ; params = pins de sortie utiles"""
        n = self._new(nid, "K2Node_Event", pos)
        n.add_pin("OutputDelegate", "delegate", "out")
        n.add_pin("then", "exec", "out")
        for pname, ptype in params:
            n.add_pin(pname, ptype, "out")
        n.props += ['EventSignatureName="%s"' % function, "EventSignatureClass=Class'%s'" % cls, "bOverrideFunction=True"]
        return n

    def call(self, nid, cls, function, pins=(), defaults=None, static=False, self_context=False, pure=False, pos=(0, 0), array=False, parent=False):
        """appel de fonction. static=True : fonction de bibliotheque (self = CDO cache) ; self_context=True : methode de this ;
        sinon methode d'un objet (pin 'self' = Target a lier). pins = [(name, type, 'in'|'out')] limitee a ce qu'on lie/defaut.
        array=True : noeud K2Node_CallArrayFunction (KismetArrayLibrary.Array_Add/Get/Length/Resize... : la pin TargetArray est
        typee par le tableau lie, format releve dans le T3D vanilla Buff_FestiveDinoColors 2026-09-11)."""
        n = self._new(nid, "K2Node_CallParentFunction" if parent else ("K2Node_CallArrayFunction" if array else "K2Node_CallFunction"), pos)
        if not pure:
            n.add_pin("execute", "exec", "in")
            n.add_pin("then", "exec", "out")
        short = cls.split(".")[-1]
        if static:
            n.add_pin("self", "object:" + cls, "in", default_object="Default__" + short, hidden=True, friendly="Target")
            n.props.append("FunctionReference=(MemberParentClass=Class'%s',MemberName=\"%s\")" % (cls, function))
        elif self_context:
            n.add_pin("self", "object:" + cls, "in", friendly="Target")
            n.props.append('FunctionReference=(MemberName="%s",bSelfContext=True)' % function)
        else:
            n.add_pin("self", "object:" + cls, "in", friendly="Target")
            n.props.append("FunctionReference=(MemberParentClass=Class'%s',MemberName=\"%s\")" % (cls, function))
        if pure:
            n.props.append("bIsPureFunc=True")
        for pname, ptype, d in pins:
            if not n.pin(pname):
                n.add_pin(pname, ptype, d)
        for k, v in (defaults or {}).items():
            p = n.pin(k) or n.add_pin(k, "wildcard", "in")
            p.default = v
        return n

    def _var_ref(self, n, name, cls):
        """cls=None : variable de this (bSelfContext). cls='/Script/X.Y' : variable d'un AUTRE objet (pin 'self' = Target a lier),
        format releve dans le T3D vanilla Buff_RageEffect.BPExcludeAoEActor (K2Node_VariableSet_3480, 2026-09-06) :
        VariableReference=(MemberParentClass=Class'...',MemberName="...") + SelfContextInfo=NotSelfContext"""
        if cls:
            n.add_pin("self", "object:" + cls, "in", friendly="Target")
            n.props.append("VariableReference=(MemberParentClass=%s'%s',MemberName=\"%s\")" % (_ref_kind(cls), cls, name))
            n.props.append("SelfContextInfo=NotSelfContext")
        else:
            n.add_pin("self", "self", "in", hidden=True)
            n.props.append('VariableReference=(MemberName="%s",bSelfContext=True)' % name)

    def var_get(self, nid, name, vtype, pos=(0, 0), cls=None):
        n = self._new(nid, "K2Node_VariableGet", pos)
        n.add_pin(name, vtype, "out")
        self._var_ref(n, name, cls)
        return n

    def var_set(self, nid, name, vtype, pos=(0, 0), cls=None):
        n = self._new(nid, "K2Node_VariableSet", pos)
        n.add_pin("execute", "exec", "in")
        n.add_pin("then", "exec", "out")
        n.add_pin(name, vtype, "in")
        self._var_ref(n, name, cls)
        return n

    def branch(self, nid, pos=(0, 0)):
        n = self._new(nid, "K2Node_IfThenElse", pos)
        n.add_pin("execute", "exec", "in")
        n.add_pin("Condition", "bool", "in", default="true")
        n.add_pin("then", "exec", "out", friendly="true")
        n.add_pin("else", "exec", "out", friendly="false")
        return n

    def sequence(self, nid, outputs=2, pos=(0, 0)):
        n = self._new(nid, "K2Node_ExecutionSequence", pos)
        n.add_pin("execute", "exec", "in")
        for i in range(outputs):
            n.add_pin("then_%d" % i, "exec", "out")
        return n

    def self_node(self, nid, pos=(0, 0)):
        n = self._new(nid, "K2Node_Self", pos)
        n.add_pin("self", "self", "out")
        return n

    def cast(self, nid, target, pos=(0, 0)):
        """K2Node_DynamicCast ; pin de sortie = 'As' + nom court de la classe (ex. AsPlayerController, AsBuff_X_C)"""
        n = self._new(nid, "K2Node_DynamicCast", pos)
        n.add_pin("execute", "exec", "in")
        n.add_pin("then", "exec", "out")
        n.add_pin("CastFailed", "exec", "out")
        n.add_pin("Object", "object:/Script/CoreUObject.Object", "in")
        n.add_pin("As" + target.split(".")[-1], "object:" + target, "out")
        n.props.append("TargetType=%s'%s'" % (_ref_kind(target), target))
        return n

    def add_delegate(self, nid, cls, delegate, pos=(0, 0)):
        """K2Node_AddDelegate = 'Bind Event to <delegate>' d'un objet (pin self = Target a lier, pin Delegate = OutputDelegate
        d'un custom_event). Format releve dans le T3D de CCA_ChatSpawner (K2Node_AddDelegate_973, 2026-09-21) ; ReconstructNode
        regenere les pins depuis DelegateReference."""
        n = self._new(nid, "K2Node_AddDelegate", pos)
        n.add_pin("execute", "exec", "in")
        n.add_pin("then", "exec", "out")
        n.add_pin("self", "object:" + cls, "in", friendly="Target")
        n.add_pin("Delegate", "delegate", "in", friendly="Event")
        n.props.append("DelegateReference=(MemberParentClass=%s'%s',MemberName=\"%s\")" % (_ref_kind(cls), cls, delegate))
        return n

    def macro_foreach(self, nid, elem, pos=(0, 0)):
        """K2Node_MacroInstance StandardMacros:ForEachLoop sur un tableau (format releve dans CCA_ProcessMessage.t3d,
        K2Node_MacroInstance_5160). elem = type d'element : '/Script/X.Y' (objet, forme historique) ou un type t3d_write
        ('string', 'object:/Script/X.Y', 'struct:...'). Pins : Exec, Array, LoopBody, 'Array Element', 'Array Index', Completed."""
        et = elem if (":" in elem or elem in SIMPLE) else "object:" + elem
        d = parse_type(et)
        n = self._new(nid, "K2Node_MacroInstance", pos)
        n.add_pin("Exec", "exec", "in")
        n.add_pin("Array", et + "[]", "in")
        n.add_pin("LoopBody", "exec", "out")
        n.add_pin("Array Element", et, "out")
        n.add_pin("Array Index", "int", "out")
        n.add_pin("Completed", "exec", "out")
        n.props.append("MacroGraphReference=(MacroGraph=EdGraph'/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForEachLoop',GraphBlueprint=Blueprint'/Engine/EditorBlueprintResources/StandardMacros.StandardMacros',GraphGuid=99DBFD5540A796041F72A5A9DA655026)")
        d2 = dict(d); d2["is_array"] = True
        n.props.append("ResolvedWildcardType=" + pin_type_text(d2))
        return n

    def class_cast(self, nid, target, pos=(0, 0)):
        """K2Node_ClassDynamicCast : Class generique -> TSubclassOf<target> (pin 'Object' in, 'As<Nom>' out, exec/then/CastFailed)"""
        n = self._new(nid, "K2Node_ClassDynamicCast", pos)
        n.add_pin("execute", "exec", "in")
        n.add_pin("then", "exec", "out")
        n.add_pin("CastFailed", "exec", "out")
        n.add_pin("Class", "class:/Script/CoreUObject.Object", "in")   # pin d'entree = "Class" (releve 2026-09-21)
        n.add_pin("As" + target.split(".")[-1], "class:" + target, "out")
        n.props.append("TargetType=%s'%s'" % (_ref_kind(target), target))
        return n

    def make_struct(self, nid, struct, fields=(), pos=(0, 0)):
        n = self._new(nid, "K2Node_MakeStruct", pos)
        n.add_pin(struct.split(":")[-1].split(".")[-1], "struct:" + struct, "out")
        for fname, ftype in fields:
            n.add_pin(fname, ftype, "in")
        n.props.append("StructType=%s'%s'" % (_ref_kind(struct), struct))
        return n

    def break_struct(self, nid, struct, fields=(), pos=(0, 0)):
        n = self._new(nid, "K2Node_BreakStruct", pos)
        n.add_pin(struct.split(":")[-1].split(".")[-1], "struct:" + struct + "&", "in")
        for fname, ftype in fields:
            n.add_pin(fname, ftype, "out")
        n.props.append("StructType=%s'%s'" % (_ref_kind(struct), struct))
        return n

    # ---- liens ---------------------------------------------------------------------------------------------------
    def link(self, a, apin, b, bpin):
        """a/b = id de noeud du lot, ou '@<NomObjet>' pour un noeud DEJA dans le graph (fait apres le paste par link_pins)"""
        if str(a).startswith("@") or str(b).startswith("@"):
            self.external_links.append((a, apin, b, bpin))
            return
        na, nb = self.get(a), self.get(b)
        if not na or not nb:
            raise ValueError("lien vers un noeud inconnu : %s -> %s" % (a, b))
        pa = na.pin(apin) or na.add_pin(apin, "wildcard", "out")
        pb = nb.pin(bpin) or nb.add_pin(bpin, "wildcard", "in")
        pa.links.append((nb, pb))
        pb.links.append((na, pa))

    # ---- sortie ----------------------------------------------------------------------------------------------------
    def _assign_pin_names(self):
        for n in self.nodes:
            for p in n.pins:
                if not p.obj_name:
                    self._p += 1
                    p.obj_name = "EdGraphPin_DK%s_%d" % (self.tag, self._p)

    def t3d(self) -> str:
        self._assign_pin_names()
        out = []
        for n in self.nodes:
            # Archetype= : sans lui le factory cherche l'objet "None" et journalise "Failed to find object 'Object None.'" (inoffensif)
            out.append('Begin Object Class=%s Name="%s" Archetype=%s\'/Script/BlueprintGraph.Default__%s\'' % (n.cls, n.obj_name, n.cls, n.cls))
            for p in n.pins:
                out.append('   Begin Object Class=EdGraphPin Name="%s"' % p.obj_name)
                out.append("   End Object")
            for p in n.pins:
                out.append('   Begin Object Name="%s"' % p.obj_name)
                out.append('      PinName=%s' % _q(p.name))
                if p.friendly:
                    out.append('      PinFriendlyName=%s' % _q(p.friendly))
                if p.dir == "out":
                    out.append("      Direction=EGPD_Output")
                out.append("      PinType=%s" % pin_type_text(p.type))
                if p.default is not None:
                    out.append("      DefaultValue=%s" % _q(p.default))
                if p.default_object:
                    do = p.default_object
                    if do.startswith("/"):
                        do = "%s'%s'" % (_ref_kind(do), do)
                    out.append("      DefaultObject=%s" % do)
                for i, (on, op) in enumerate(p.links):
                    out.append("      LinkedTo(%d)=EdGraphPin'\"%s.%s\"'" % (i, on.obj_name, op.obj_name))
                if p.hidden:
                    out.append("      bHidden=True")
                out.append("   End Object")
            out += ["   " + s for s in n.props]
            for i, p in enumerate(n.pins):
                out.append("   Pins(%d)=EdGraphPin'%s'" % (i, p.obj_name))
            out.append("   NodePosX=%d" % n.pos[0])
            out.append("   NodePosY=%d" % n.pos[1])
            out += ["   " + s for s in n.custom]
            out.append("End Object")
        return "\n".join(out) + "\n"

    def obj_names(self) -> dict:
        return {n.id: n.obj_name for n in self.nodes}

    # ---- depuis une spec JSON ------------------------------------------------------------------------------------
    @classmethod
    def from_spec(cls, spec):
        if isinstance(spec, str):
            spec = json.loads(spec)
        g = cls(spec.get("tag"))
        for nd in spec.get("nodes", []):
            t, nid, pos = nd["type"], nd["id"], nd.get("pos", (0, 0))
            if t == "custom_event":
                g.custom_event(nid, nd["name"], nd.get("params", ()), nd.get("net"), nd.get("reliable", True), pos)
            elif t == "event":
                g.event(nid, nd["class"], nd["function"], nd.get("params", ()), pos)
            elif t in ("call", "call_array", "call_parent"):
                g.call(nid, nd["class"], nd["function"], nd.get("pins", ()), nd.get("defaults"), nd.get("static", False),
                       nd.get("self_context", False), nd.get("pure", False), pos, array=(t == "call_array"), parent=(t == "call_parent"))
            elif t == "var_get":
                g.var_get(nid, nd["name"], nd.get("vtype", "wildcard"), pos, nd.get("cls"))
            elif t == "var_set":
                g.var_set(nid, nd["name"], nd.get("vtype", "wildcard"), pos, nd.get("cls"))
            elif t == "branch":
                g.branch(nid, pos)
            elif t == "sequence":
                g.sequence(nid, nd.get("outputs", 2), pos)
            elif t == "self":
                g.self_node(nid, pos)
            elif t == "cast":
                g.cast(nid, nd["target"], pos)
            elif t == "add_delegate":
                g.add_delegate(nid, nd["class"], nd["delegate"], pos)
            elif t == "macro_foreach":
                g.macro_foreach(nid, nd["elem"], pos)
            elif t == "class_cast":
                g.class_cast(nid, nd["target"], pos)
            elif t == "make_struct":
                g.make_struct(nid, nd["struct"], nd.get("fields", ()), pos)
            elif t == "break_struct":
                g.break_struct(nid, nd["struct"], nd.get("fields", ()), pos)
            else:
                raise ValueError("type de noeud inconnu : " + t)
            n = g.get(nid)
            for k, v in (nd.get("defaults") or {}).items():
                p = n.pin(k) or n.add_pin(k, "wildcard", "in")
                p.default = v
            for k, v in (nd.get("default_objects") or {}).items():   # pin objet/classe : chemin complet (ex. classe BP '.../X.X_C')
                p = n.pin(k) or n.add_pin(k, "wildcard", "in")
                p.default_object = v
        for l in spec.get("links", []):
            g.link(*l)
        return g


if __name__ == "__main__":
    import sys
    spec = json.load(open(sys.argv[1], encoding="utf-8")) if len(sys.argv) > 1 else {
        "nodes": [{"id": "ev", "type": "custom_event", "name": "Ping", "params": [["Count", "int"]], "net": "server"},
                  {"id": "c", "type": "call", "class": "/Script/Engine.KismetSystemLibrary", "function": "PrintString",
                   "static": True, "defaults": {"InString": "hello"}, "pos": [400, 0]}],
        "links": [["ev", "then", "c", "execute"]]}
    g = GraphBuilder.from_spec(spec)
    sys.stdout.write(g.t3d())
    sys.stderr.write("externes : %r\n" % g.external_links)
