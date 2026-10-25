#!/usr/bin/env python3
"""check_dll.py - verifie la DLL DevKitApi buildee avant deploiement :
export InitializeModule, ressource RCDATA 191 = "0", imports limites aux DLL systeme (aucun UE4Editor-*,
aucun runtime VC MSVCP140/VCRUNTIME140 -> build /MT), PDB a cote avec chemin CodeView coherent.
Usage : python check_dll.py [chemin.dll]   (defaut : ..\\build\\UE4Editor-DevKitApi.dll)
"""
import os, sys, pefile

SYSTEM_OK = {"kernel32.dll", "user32.dll", "ws2_32.dll", "advapi32.dll", "ntdll.dll"}
EXPECTED_ROOTS = ["/Game/Mods/DevKitApiTest/"]   # informational: the whitelist file next to the DLL is what counts

def main(path):
    pe = pefile.PE(path)
    ok = True
    def chk(cond, msg):
        nonlocal ok
        print(("  OK  " if cond else "  KO  ") + msg)
        ok = ok and cond
    print("==", path, os.path.getsize(path), "octets")
    chk(pe.FILE_HEADER.Machine == 0x8664, "x64")
    exps = [s.name.decode() for s in pe.DIRECTORY_ENTRY_EXPORT.symbols] if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") else []
    chk(exps == ["InitializeModule"], f"exports = {exps}")
    imps = [d.dll.decode().lower() for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])]
    for d in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        print(f"       import {d.dll.decode():20s} {len(d.imports)} symboles")
    chk(not any(i.startswith("ue4editor") for i in imps), "aucun import UE4Editor-*")
    chk(not any(("msvcp" in i) or ("vcruntime" in i) or i.startswith("api-ms-win-crt") for i in imps),
        "aucun runtime VC importe (CRT statique /MT)")
    chk(all(i in SYSTEM_OK for i in imps), "imports limites aux DLL systeme")
    res = {}
    for t in getattr(pe, "DIRECTORY_ENTRY_RESOURCE", type("x", (), {"entries": []})).entries:
        for e in t.directory.entries:
            for l in e.directory.entries:
                d = l.data.struct
                res[(t.id, e.id)] = pe.get_data(d.OffsetToData, d.Size)
    r191 = res.get((10, 191))
    chk(r191 is not None and r191.rstrip(b"\x00") == b"0" and r191.endswith(b"\x00"), f"RCDATA 191 = {r191!r}")
    chk(len(res) == 1, f"aucune autre ressource ({len(res)} au total)")
    cv = ""
    for dbg in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []):
        if hasattr(dbg.entry, "PdbFileName"):
            cv = dbg.entry.PdbFileName.decode(errors="replace").rstrip("\x00")
    pdb = os.path.splitext(path)[0] + ".pdb"
    chk(os.path.isfile(pdb), f"PDB a cote : {pdb}")
    chk(os.path.normcase(cv) == os.path.normcase(os.path.abspath(pdb)), f"CodeView -> {cv}")
    # Liste blanche externe (2026-09-07) : fichier a cote de la DLL + la DLL doit connaitre son nom
    wl = os.path.join(os.path.dirname(path), "DevKitApi_whitelist.txt")
    chk(os.path.isfile(wl), f"DevKitApi_whitelist.txt a cote : {wl}")
    roots = []
    if os.path.isfile(wl):
        for line in open(wl, encoding="utf-8-sig"):
            t = line.split("#", 1)[0].strip().replace("\\", "/")
            if not t:
                continue
            if not t.endswith("/"):
                t += "/"
            if t.startswith("/Game/") and len(t) > 6:
                roots.append(t)
            else:
                print("       ligne ignoree :", t)
    chk(len(roots) > 0, f"whitelist roots next to the DLL = {roots}")
    chk("DevKitApi_whitelist.txt".encode("utf-16-le") in open(path, "rb").read(), "la DLL reference DevKitApi_whitelist.txt (chargement au demarrage)")
    print("RESULTAT :", "TOUT OK" if ok else "ECHEC")
    return 0 if ok else 1

if __name__ == "__main__":
    p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "UE4Editor-DevKitApi.dll")
    sys.exit(main(os.path.abspath(p)))
