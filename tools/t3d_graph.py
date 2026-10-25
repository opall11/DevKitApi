#!/usr/bin/env python3
"""t3d_graph.py - lit un export T3D du DevKit (obj export type=Object name=<objet> file=x.t3d) et le rend exploitable :
  - parse(path)            : arbre d'objets (classe, nom, proprietes, sous-objets)
  - obj_to_dict(obj)       : arbre -> dict JSON-able
  - flatten(obj)/diff(a,b) : proprietes aplaties "Sous/Objet/Prop(3)" et diff entre deux objets (ex. CDO enfant / parent)
  - Graph(root)            : vue "graph Blueprint" (noeuds, pins, liens) ; graph_to_dict(g) ; pseudo(g) -> texte
CLI : python t3d_graph.py <fichier.t3d> [--raw | --json]
"""
import json
import re
import sys

RE_BEGIN = re.compile(r'^\s*Begin Object(?:\s+Class=([\w.\'/]+))?\s+Name="([^"]+)"')
RE_END = re.compile(r'^\s*End Object')
RE_PROP = re.compile(r'^\s*([A-Za-z_][\w]*)(?:\((\d+)\))?=(.*)$')
RE_SHORT = re.compile(r"\w+'\"?/[^'\"]*[./](\w+)\"?'")
RE_LAST = re.compile(r".*[./]([\w]+)'?$")


class Obj:
    def __init__(self, cls, name, parent):
        self.cls, self.name, self.parent = cls, name, parent
        self.props = {}
        self.children = {}

    def get(self, k, d=None):
        return self.props.get(k, d)

    def arr(self, k):
        v = self.props.get(k)
        if isinstance(v, dict):
            return [v[i] for i in sorted(v)]
        return [] if v is None else [v]


def read_text(path):
    raw = open(path, 'rb').read()
    if raw.startswith(b'\xff\xfe'):
        return raw.decode('utf-16', 'replace')
    if raw.startswith(b'\xef\xbb\xbf'):
        return raw[3:].decode('utf-8', 'replace')
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('cp1252', 'replace')


def parse(path):
    root = Obj('ROOT', 'ROOT', None)
    stack = [root]
    for line in read_text(path).splitlines():
        m = RE_BEGIN.match(line)
        if m:
            cls, name = m.group(1), m.group(2)
            cur = stack[-1]
            if name in cur.children:
                o = cur.children[name]
                if cls and not o.cls:
                    o.cls = cls
            else:
                o = Obj(cls, name, cur)
                cur.children[name] = o
            stack.append(o)
            continue
        if RE_END.match(line):
            if len(stack) > 1:
                stack.pop()
            continue
        m = RE_PROP.match(line)
        if m and len(stack) > 1:
            k, idx, v = m.group(1), m.group(2), m.group(3)
            o = stack[-1]
            if idx is not None:
                o.props.setdefault(k, {})[int(idx)] = v
            else:
                o.props[k] = v
    return root


def first(root):
    """l'objet exporte (premier enfant de la racine)"""
    return next(iter(root.children.values()))


def obj_to_dict(o):
    props = {}
    for k, v in o.props.items():
        if isinstance(v, dict):
            idx = sorted(v)
            props[k] = [v[i] for i in idx] if idx == list(range(len(idx))) else {str(i): v[i] for i in idx}
        else:
            props[k] = v
    return {'class': o.cls, 'name': o.name, 'props': props,
            'subobjects': [obj_to_dict(c) for c in o.children.values()]}


def flatten(o, prefix=''):
    """{'Sub/Prop(3)': valeur} sur tout l'arbre (racine exclue du prefixe)"""
    out = {}
    for k, v in o.props.items():
        if isinstance(v, dict):
            for i in sorted(v):
                out['%s%s(%d)' % (prefix, k, i)] = v[i]
        else:
            out[prefix + k] = v
    for c in o.children.values():
        out.update(flatten(c, prefix + c.name + '/'))
    return out


def diff(a, b):
    """a = enfant, b = parent (ou reference). Renvoie only_a / only_b / changed"""
    fa, fb = flatten(a), flatten(b)
    return {
        'only_a': {k: fa[k] for k in fa if k not in fb},
        'only_b': {k: fb[k] for k in fb if k not in fa},
        'changed': {k: {'a': fa[k], 'b': fb[k]} for k in fa if k in fb and fa[k] != fb[k]},
    }


def unq(s):
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        return s[1:-1]
    return s


def last(s):
    m = RE_LAST.match(s or '')
    return m.group(1) if m else (s or '')


def parse_struct(s):
    """(A=1,B="x",C=(..)) -> dict (1 niveau, valeurs brutes)"""
    s = s.strip()
    if not (s.startswith('(') and s.endswith(')')):
        return {}
    s = s[1:-1]
    out = {}
    depth = 0
    cur = ''
    parts = []
    inq = False
    for ch in s:
        if ch == '"':
            inq = not inq
        if not inq:
            if ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ',' and depth == 0:
                parts.append(cur)
                cur = ''
                continue
        cur += ch
    if cur:
        parts.append(cur)
    for p in parts:
        if '=' in p:
            k, v = p.split('=', 1)
            out[k.strip()] = v.strip()
    return out


class Pin:
    def __init__(self, o, node):
        self.o, self.node = o, node
        self.name = unq(o.get('PinName', '""'))
        self.dir = 'out' if o.get('Direction') == 'EGPD_Output' else 'in'
        pt = parse_struct(o.get('PinType', '()'))
        self.cat = unq(pt.get('PinCategory', '""'))
        self.sub = pt.get('PinSubCategoryObject', pt.get('PinSubCategory', ''))
        self.is_array = pt.get('bIsArray') == 'True'
        self.default = o.get('DefaultValue')
        self.defobj = o.get('DefaultObject')
        self.deftext = o.get('DefaultTextValue')
        self.hidden = o.get('bHidden') == 'True'
        self.links = []
        for l in o.arr('LinkedTo'):
            mm = re.search(r"([\w]+)\.(EdGraphPin_[\w]+)", l)   # noms de noeuds/pins libres (nos noeuds colles : DK<hex>_..., EdGraphPin_DK<hex>_n)
            if mm:
                self.links.append((mm.group(1), mm.group(2)))

    def literal(self):
        if self.default is not None:
            return unq(self.default)
        if self.deftext is not None:
            return 'TEXT:' + unq(self.deftext)
        if self.defobj is not None:
            return unq(self.defobj)
        return None

    def to_dict(self):
        d = {'id': self.o.name, 'name': self.name, 'direction': self.dir, 'category': self.cat}
        if self.sub:
            d['sub'] = short(self.sub)
        if self.is_array:
            d['array'] = True
        lit = self.literal()
        if lit is not None:
            d['default'] = lit
        if self.hidden:
            d['hidden'] = True
        if self.links:
            d['links'] = ['%s.%s' % l for l in self.links]
        return d


NODE_PROPS = ('FunctionReference', 'VariableReference', 'TargetType', 'EventReference', 'EventSignatureName',
              'CustomFunctionName', 'SignatureName', 'MacroGraphReference', 'DelegateReference',
              'SelectedFunctionName', 'StructType', 'TimelineName', 'BoundGraph', 'ObjectRef', 'NodeComment',
              'bIsPureFunc', 'bIsConstFunc', 'bIsEditable', 'ExtraFlags')


class Node:
    def __init__(self, o):
        self.o = o
        self.name = o.name
        self.cls = o.cls or '?'
        self.pins = {}
        self.pinlist = []
        for cn, co in o.children.items():
            if cn.startswith('EdGraphPin'):
                p = Pin(co, self)
                self.pins[cn] = p
                self.pinlist.append(p)
        self.posx = int(o.get('NodePosX', '0') or 0)
        self.posy = int(o.get('NodePosY', '0') or 0)

    def pin(self, name, d=None):
        for p in self.pinlist:
            if p.name == name and (d is None or p.dir == d):
                return p
        return None

    def exec_ins(self):
        return [p for p in self.pinlist if p.cat == 'exec' and p.dir == 'in']

    def exec_outs(self):
        return [p for p in self.pinlist if p.cat == 'exec' and p.dir == 'out']

    def is_pure(self):
        return not self.exec_ins() and not self.exec_outs()

    def label(self):
        c = self.cls.replace('K2Node_', '')
        o = self.o
        if c in ('CallFunction', 'CallArrayFunction', 'CommutativeAssociativeBinaryOperator', 'CallParentFunction'):
            fr = parse_struct(o.get('FunctionReference', '()'))
            cls = last(fr.get('MemberParentClass', ''))
            nm = unq(fr.get('MemberName', '?'))
            if fr.get('bSelfContext') == 'True':
                cls = 'self'
            return ('PARENT::' if c == 'CallParentFunction' else '') + (cls + '.' if cls else '') + nm
        if c in ('VariableGet', 'VariableSet'):
            vr = parse_struct(o.get('VariableReference', '()'))
            cls = last(vr.get('MemberParentClass', ''))
            nm = unq(vr.get('MemberName', '?'))
            pre = (cls + '.') if cls and vr.get('bSelfContext') != 'True' else ''
            return ('GET ' if c == 'VariableGet' else 'SET ') + pre + nm
        if c in ('DynamicCast', 'ClassDynamicCast'):
            return 'CAST<' + last(o.get('TargetType', '?')) + '>'
        if c == 'Event':
            er = parse_struct(o.get('EventReference', '()'))
            nm = unq(er.get('MemberName', '')) or unq(o.get('EventSignatureName', '?'))
            return 'EVENT ' + nm
        if c == 'CustomEvent':
            return 'CUSTOM EVENT ' + unq(o.get('CustomFunctionName', '?'))
        if c == 'FunctionEntry':
            return 'FUNCTION ' + unq(o.get('SignatureName', '?'))
        if c == 'FunctionResult':
            return 'RETURN'
        if c == 'MacroInstance':
            mg = o.get('MacroGraphReference', '')
            mm = re.search(r"MacroGraph=EdGraph'\"?([^'\"]+)\"?'", mg)
            return 'MACRO ' + (mm.group(1).split(':')[-1] if mm else '?')
        table = {'IfThenElse': 'BRANCH', 'ExecutionSequence': 'SEQUENCE', 'Select': 'SELECT',
                 'SwitchString': 'SWITCH(string)', 'SwitchInteger': 'SWITCH(int)', 'SwitchEnum': 'SWITCH(enum)',
                 'SwitchName': 'SWITCH(name)', 'SpawnActorFromClass': 'SPAWN ACTOR', 'SpawnActor': 'SPAWN ACTOR',
                 'GetArrayItem': 'ARRAY[]', 'MakeArray': 'MAKE ARRAY', 'Self': 'self', 'Knot': 'REROUTE',
                 'Tunnel': 'TUNNEL'}
        if c in table:
            return table[c]
        if c == 'AddDelegate':
            dr = parse_struct(o.get('DelegateReference', '()'))
            return 'BIND ' + unq(dr.get('MemberName', '?'))
        if c == 'CreateDelegate':
            return 'DELEGATE ' + unq(o.get('SelectedFunctionName', '?'))
        if c == 'Timeline':
            return 'TIMELINE ' + unq(o.get('TimelineName', '?'))
        if c == 'MakeStruct':
            return 'MAKE ' + last(o.get('StructType', '?'))
        if c == 'BreakStruct':
            return 'BREAK ' + last(o.get('StructType', '?'))
        if c == 'Literal':
            return 'LITERAL ' + o.get('ObjectRef', '?')
        if c == 'Composite':
            return 'COLLAPSED ' + o.get('BoundGraph', '?').split(':')[-1]
        return c

    def to_dict(self):
        d = {'id': self.name, 'class': self.cls, 'label': self.label(), 'pos': [self.posx, self.posy],
             'pure': self.is_pure()}
        props = {k: self.o.props[k] for k in NODE_PROPS if k in self.o.props}
        if props:
            d['props'] = props
        d['pins'] = [p.to_dict() for p in self.pinlist]
        return d


class Graph:
    def __init__(self, root):
        g = first(root) if root.cls == 'ROOT' else root
        self.obj = g
        self.name = g.name
        self.nodes = {}
        for cn, co in g.children.items():
            if cn.startswith('K2Node') or cn.startswith('EdGraphNode') or (co.cls or '').startswith(('K2Node', 'EdGraphNode')):   # noeuds colles par la DLL : nom DK<hex>_..., classe K2Node_*
                self.nodes[cn] = Node(co)
        self.pinowner = {}
        for n in self.nodes.values():
            for pn in n.pins:
                self.pinowner[(n.name, pn)] = n.pins[pn]

    def resolve(self, nodename, pinname):
        return self.pinowner.get((nodename, pinname))

    def broken_links(self):
        out = []
        for n in self.nodes.values():
            for p in n.pinlist:
                for l in p.links:
                    if self.resolve(*l) is None:
                        out.append('%s.%s -> %s.%s' % (n.name, p.o.name, l[0], l[1]))
        return out


def graph_to_dict(g):
    nodes = sorted(g.nodes.values(), key=lambda n: (n.posy, n.posx))
    return {'name': g.name, 'node_count': len(nodes), 'nodes': [n.to_dict() for n in nodes],
            'broken_links': g.broken_links()}


def short(s):
    return RE_SHORT.sub(r'\1', s)


def expr(g, pin, depth=0, seen=None):
    seen = seen or set()
    if pin is None:
        return '?'
    if pin.links:
        nn, pn = pin.links[0]
        src = g.resolve(nn, pn)
        if src is None:
            return '<%s.%s>' % (nn, pn)
        n = src.node
        if depth > 6 or n.name in seen:
            return '<%s.%s>' % (n.label(), src.name)
        if n.is_pure():
            return pure_expr(g, n, src, depth + 1, seen | {n.name})
        if src.name in ('ReturnValue', 'Output', 'AsObject') or src.name.startswith('As'):
            return '<%s [%s]>' % (n.label(), n.name)
        return '<%s.%s [%s]>' % (n.label(), src.name, n.name)
    lit = pin.literal()
    if lit is None or lit == '':
        if pin.cat == 'object' and pin.name == 'self':
            return 'self'
        if pin.cat in ('string', 'text', 'name', 'object', 'class'):
            return '(vide)'
        if pin.cat in ('int', 'float', 'byte'):
            return '0'
        return '?'
    return short(lit) if pin.cat != 'string' else '"' + lit + '"'


OPS = {'Add_IntInt': '+', 'Add_FloatFloat': '+', 'Multiply_FloatFloat': '*', 'Multiply_IntInt': '*',
       'BooleanAND': 'AND', 'BooleanOR': 'OR', 'Concat_StrStr': '..', 'Subtract_IntInt': '-',
       'Subtract_FloatFloat': '-'}


def pure_expr(g, n, outpin, depth, seen):
    c = n.cls.replace('K2Node_', '')
    lab = n.label()
    if c == 'VariableGet':
        tgt = n.pin('self', 'in')
        base = lab[4:]
        if tgt and tgt.links:
            return expr(g, tgt, depth, seen) + '.' + base
        return base
    if c == 'Self':
        return 'self'
    if c in ('DynamicCast', 'ClassDynamicCast'):
        inner = expr(g, n.pin('Object', 'in'), depth, seen)
        return '%s(%s)' % (lab, inner) + ('' if outpin.name.startswith('As') else '.' + outpin.name)
    if c == 'Literal':
        return lab
    ins = [p for p in n.pinlist if p.dir == 'in' and not p.hidden and p.cat != 'exec']
    if c == 'CommutativeAssociativeBinaryOperator':
        op = OPS.get(lab.split('.')[-1], lab.split('.')[-1])
        return '(' + (' %s ' % op).join(expr(g, p, depth, seen) for p in ins) + ')'
    args = ', '.join('%s=%s' % (p.name, expr(g, p, depth, seen)) for p in ins)
    suffix = '' if outpin.name in ('ReturnValue', 'Output') else '.' + outpin.name
    return '%s(%s)%s' % (lab, args, suffix)


ENTRY_KINDS = ('Event', 'CustomEvent', 'FunctionEntry', 'Tunnel', 'InputAction', 'InputKey')


def pseudo(g):
    """pseudo-code texte : suit les pins exec depuis chaque entree, expressions pures en ligne"""
    out = []
    entries = [n for n in g.nodes.values() if n.cls.replace('K2Node_', '') in ENTRY_KINDS and not n.exec_ins()]
    entries.sort(key=lambda n: (n.posy, n.posx))
    visited = set()

    def emit(n, ind, via=''):
        pad = '  ' * ind
        if n.name in visited:
            out.append('%s%s-> (retour vers %s [%s])' % (pad, via, n.label(), n.name))
            return
        visited.add(n.name)
        c = n.cls.replace('K2Node_', '')
        ins = [p for p in n.pinlist if p.dir == 'in' and p.cat != 'exec' and not p.hidden]
        outs = [p for p in n.pinlist if p.dir == 'out' and p.cat != 'exec' and not p.hidden]
        if c in ('Event', 'CustomEvent', 'FunctionEntry'):
            params = ', '.join(p.name + ':' + p.cat for p in outs)
            out.append('%s%s%s(%s)   [%s]' % (pad, via, n.label(), params, n.name))
        else:
            args = ', '.join('%s=%s' % (p.name, expr(g, p)) for p in ins)
            out.append('%s%s%s(%s)   [%s]' % (pad, via, n.label(), args, n.name))
        eos = n.exec_outs()
        if not eos:
            return
        if len(eos) == 1:
            p = eos[0]
            if p.links:
                tgt = g.resolve(*p.links[0])
                if tgt:
                    emit(tgt.node, ind)
            return
        for p in eos:
            if p.links:
                tgt = g.resolve(*p.links[0])
                out.append('%s  [%s]' % (pad, p.name))
                if tgt:
                    emit(tgt.node, ind + 2)
            else:
                out.append('%s  [%s] -> (rien)' % (pad, p.name))

    for e in entries:
        out.append('')
        out.append('=' * 100)
        emit(e, 0)
    orphans = [n for n in g.nodes.values() if n.name not in visited and not n.is_pure()]
    if orphans:
        out.append('')
        out.append('--- noeuds exec NON atteints depuis une entree (deconnectes) ---')
        for n in sorted(orphans, key=lambda n: (n.posy, n.posx)):
            if n.name not in visited:
                emit(n, 0)
    return '\n'.join(out)


def raw(g):
    out = []
    for n in sorted(g.nodes.values(), key=lambda n: (n.posy, n.posx)):
        out.append('%s [%s] %s @(%d,%d)' % (n.name, n.cls, n.label(), n.posx, n.posy))
        for p in n.pinlist:
            l = ' '.join('%s.%s' % (a, b) for a, b in p.links)
            out.append('    %-3s %-8s %-28s def=%r -> %s' % (p.dir, p.cat, p.name, p.literal(), l))
    return '\n'.join(out)


if __name__ == '__main__':
    path = sys.argv[1]
    root = parse(path)
    sys.stdout.reconfigure(encoding='utf-8')
    if '--json' in sys.argv:
        g = Graph(root)
        print(json.dumps(graph_to_dict(g) if g.nodes else obj_to_dict(first(root)), indent=1, ensure_ascii=False))
    else:
        g = Graph(root)
        print('### Graph %s : %d noeuds  (%s)' % (g.name, len(g.nodes), path))
        print(raw(g) if '--raw' in sys.argv else pseudo(g))
