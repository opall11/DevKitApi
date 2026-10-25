import json, socket
HOST, PORT = "127.0.0.1", 9877
_id = [1000]
def send(obj, timeout=180.0):
    _id[0] += 1
    obj = dict(obj); obj['id'] = _id[0]
    line = json.dumps(obj, ensure_ascii=False)
    with socket.create_connection((HOST, PORT), timeout=timeout) as s:
        s.sendall((line + "\n").encode("utf-8"))
        buf = b""
        while b"\n" not in buf:
            c = s.recv(1 << 20)
            if not c: break
            buf += c
    return json.loads(buf.decode("utf-8", "replace").strip())

def get_prop(obj, prop, cdo=True):
    return send({"cmd": "get_property", "object": obj, "property": prop, "index": 0, "cdo": cdo})

def set_prop(obj, prop, value, cdo=True):
    return send({"cmd": "set_property", "object": obj, "property": prop, "value": value, "index": 0, "cdo": cdo})

def pie_running():
    """True si un monde PIE (UEDPIE_) est charge dans l'editeur."""
    r = send({"cmd": "exec", "command": "obj list class=World", "to_log": False})
    return "UEDPIE_" in ((r.get("result") or {}).get("output") or "")

def assert_no_pie():
    """A appeler en tete de tout script qui ECRIT (compile, set_property, save...). Leve si un PIE tourne."""
    if pie_running():
        raise SystemExit("PIE EN COURS : aucune ecriture tant qu'Opall n'a pas ferme le Play In Editor.")
