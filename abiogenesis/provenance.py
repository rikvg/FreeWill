"""Summarise the provenance of replicator species from `soup analyze -json` output.

For each species, positions are split into "essential" (knocking the byte out
halves the self-replication score) and the rest. For each group we report
where the bytes' values were born: the initial random soup (and from how many
distinct initial tapes), a background mutation, or an arithmetic edit made by
some program's '+'/'-'.

usage: python3 provenance.py analysis.json [more.json ...]
"""
import json
import sys
from collections import Counter

KINDS = {0: "initial", 1: "mutation", 2: "edit"}
OPS = set(b"<>{}+-.,[]")


def render(hexstr):
    out = []
    for b in bytes.fromhex(hexstr):
        out.append(chr(b) if b in OPS else ("0" if b == 0 else "·"))
    return "".join(out)


def summarise(sp):
    prog = bytes.fromhex(sp["hex"])
    score = sp["score"]
    ess = [j for j, k in enumerate(sp["knockout"]) if k < score / 2]
    tags = sp["tags"]
    res = {"essential_positions": ess, "program": render(sp["hex"])}
    for name, idx in (("essential", ess), ("all", range(64))):
        kinds = Counter(KINDS[tags[j][1]] for j in idx)
        init_srcs = {tags[j][2] for j in idx if tags[j][1] == 0}
        births = [tags[j][0] for j in idx]
        res[name] = {
            "n": len(list(idx)),
            "kinds": dict(kinds),
            "distinct_initial_tapes": len(init_srcs),
            "distinct_sources": len({(tags[j][1], tags[j][2], tags[j][0]) for j in idx}),
            "birth_epoch_min": min(births) if births else None,
            "birth_epoch_max": max(births) if births else None,
        }
    res["essential_ops"] = "".join(chr(prog[j]) if prog[j] in OPS else "?" for j in ess)
    return res


def main(paths):
    out = []
    for p in paths:
        d = json.load(open(p))
        for rank, sp in enumerate(d["species"], 1):
            s = summarise(sp)
            s.update(file=p, epoch=d["epoch"], rank=rank, count=sp["count"], score=sp["score"])
            out.append(s)
            e, a = s["essential"], s["all"]
            print(f"{p} epoch {d['epoch']} #{rank} count {sp['count']} score {sp['score']}")
            print(f"  {s['program']}")
            print("  " + "".join("^" if j in s["essential_positions"] else " " for j in range(64)))
            print(f"  essential {e['n']:2d} bytes: {e['kinds']}  from {e['distinct_initial_tapes']} initial tapes,"
                  f" {e['distinct_sources']} distinct birth events, born epochs {e['birth_epoch_min']}..{e['birth_epoch_max']}")
            print(f"  whole tape     : {a['kinds']}  from {a['distinct_initial_tapes']} initial tapes,"
                  f" {a['distinct_sources']} distinct birth events")
    return out


if __name__ == "__main__":
    main(sys.argv[1:])
