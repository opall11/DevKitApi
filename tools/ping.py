#!/usr/bin/env python3
r"""ping.py - client de test du socket DevKitApi (127.0.0.1:9877).

Usage :
  python ping.py                       -> {"id":1,"cmd":"ping"}
  python ping.py log "hello depuis Claude"   -> commande log (ligne [DevKitApi] hello... dans l'Output Log)
  python ping.py editor_ready
  python ping.py raw '{"id":9,"cmd":"xxx"}'  -> envoie le JSON tel quel
"""
import json, socket, sys

HOST, PORT = "127.0.0.1", 9877

def send(obj_or_str, timeout=35.0):
    line = obj_or_str if isinstance(obj_or_str, str) else json.dumps(obj_or_str, ensure_ascii=False)
    with socket.create_connection((HOST, PORT), timeout=timeout) as s:
        s.sendall((line + "\n").encode("utf-8"))
        buf = b""
        while b"\n" not in buf:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    return buf.decode("utf-8", "replace").strip()

def main(argv):
    if not argv:
        req = {"id": 1, "cmd": "ping"}
    elif argv[0] == "raw":
        req = argv[1]
    elif argv[0] == "log":
        req = {"id": 2, "cmd": "log", "text": " ".join(argv[1:]) or "hello"}
    else:
        req = {"id": 3, "cmd": argv[0]}
    try:
        print(">>", req if isinstance(req, str) else json.dumps(req, ensure_ascii=False))
        print("<<", send(req))
    except ConnectionRefusedError:
        print("connexion refusee : le DevKit ne tourne pas ou la DLL n'est pas chargee (voir devkitapi.log / ShooterGame.log)")
        return 1
    except socket.timeout:
        print("timeout : socket ouvert mais pas de reponse (thread principal de l'editeur occupe ?)")
        return 2
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
