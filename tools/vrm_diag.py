#!/usr/bin/env python3
"""Vergleicht, welche Werte VRM für zwei Installationen gespeichert hat.

Liest nur. Nutzt die VRM-API (vrmapi.victronenergy.com, Endpunkt diagnostics)
mit einem Zugriffstoken aus VRM (Einstellungen -> Integrationen -> Zugriffstoken).
Das Token steht in tools/vrm_api_token.txt (nicht im Repo).

  python3 tools/vrm_diag.py                 # listet die Installationen
  python3 tools/vrm_diag.py <idSite> <idSite>   # vergleicht zwei Installationen

Ausgabe je Gerät (Dienst, Instanz): welche Codes nur in der einen,
nur in der anderen oder in beiden Installationen vorkommen.
"""

import json
import os
import sys
import urllib.request

API = "https://vrmapi.victronenergy.com/v2"
TOKEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vrm_api_token.txt")


def hole(pfad):
    tok = open(TOKEN).read().strip()
    req = urllib.request.Request(API + pfad, headers={"X-Authorization": "Token " + tok})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def installationen():
    me = hole("/users/me")["user"]["id"]
    for s in hole(f"/users/{me}/installations")["records"]:
        print(s["idSite"], s.get("name"))


def diagnose(site):
    d = hole(f"/installations/{site}/diagnostics?count=1000")
    geraete = {}
    for r in d["records"]:
        key = (r.get("dbusServiceType") or r.get("Device") or "?", r.get("instance"))
        geraete.setdefault(key, {})[r.get("code")] = r.get("description")
    return geraete


def vergleich(a, b):
    ga, gb = diagnose(a), diagnose(b)
    for key in sorted(set(ga) | set(gb), key=lambda k: (str(k[0]), str(k[1]))):
        ca, cb = ga.get(key, {}), gb.get(key, {})
        beide = sorted(set(ca) & set(cb))
        nur_a = sorted(set(ca) - set(cb))
        nur_b = sorted(set(cb) - set(ca))
        print(f"\n== {key[0]} [{key[1]}]: {len(ca)} Codes in {a}, {len(cb)} in {b}")
        if beide:
            print("  beide:", " ".join(beide))
        if nur_a:
            print(f"  nur {a}:")
            for c in nur_a:
                print(f"    {c:8s} {ca[c]}")
        if nur_b:
            print(f"  nur {b}:")
            for c in nur_b:
                print(f"    {c:8s} {cb[c]}")


def main():
    if not os.path.exists(TOKEN):
        sys.exit(f"fehlt: {TOKEN}")
    if len(sys.argv) == 1:
        installationen()
    elif len(sys.argv) == 3:
        vergleich(sys.argv[1], sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
