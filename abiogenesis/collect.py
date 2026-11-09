"""Collect raw run outputs and snapshots into small files under results/.

Needs the seed-1 snapshots in runs/keep/ (not in git; regenerate with
runs/batch.sh 1). Writes hero.json, provenance_stats.json, replays.csv,
small_runs.csv.
"""
import csv
import glob
import json
import os
import re

import numpy as np

os.chdir(os.path.dirname(os.path.abspath(__file__)))


def load(path):
    with open(path, "rb") as f:
        n = int(np.frombuffer(f.read(32), "<u8")[1])
        soup = np.frombuffer(f.read(n * 64), np.uint8).reshape(n, 64)
        tags = np.frombuffer(f.read(n * 64 * 8), "<u8").reshape(n, 64)
    return soup, tags


# Hero: 256 programs from just before and just after takeover.
rng = np.random.default_rng(7)
hero = {}
for name, e in (("before", 11520), ("after", 11776)):
    s, _ = load(f"runs/keep/s1_{e}.bin")
    pick = rng.choice(len(s), 256, replace=False)
    hero[name] = [s[i].tobytes().hex() for i in sorted(pick)]
json.dump(hero, open("results/hero.json", "w"))

# Provenance background and the dominant replicator.
s, t = load("runs/keep/s1_11520.bin")
kind = (t >> 30) & 3
ep = (t >> 32).astype(np.int64)
fr = np.bincount(s.ravel(), minlength=256) / s.size
p_loaded = float(np.prod([fr[c] for c in b"[,}<]]<},["]))
raw = s.tobytes()
frag = sum(1 for i in range(len(s)) if b",}<" in raw[i * 64:(i + 1) * 64] or b"<}," in raw[i * 64:(i + 1) * 64])
d = json.load(open("results/s1_11776.json"))
sp = d["species"][0]
ess = [j for j, k in enumerate(sp["knockout"]) if k < sp["score"] / 2]
tags = sp["tags"]
lot = open("results/lottery.txt").read()
m = re.search(r" 1:(\d+)", lot)
stats = dict(
    init_frac=float((kind == 0).mean()), mut_frac=float((kind == 1).mean()), edit_frac=float((kind == 2).mean()),
    median_age=int(11520 - np.median(ep)),
    freq_lt=float(fr[ord("<")]), freq_comma=float(fr[ord(",")]), p_loaded=p_loaded, frag=frag,
    rep_edit=sum(1 for x in tags if x[1] == 2), rep_mut=sum(1 for x in tags if x[1] == 1),
    ess_events=len({tuple(tags[j]) for j in ess}),
    ess_min=min(tags[j][0] for j in ess), ess_max=max(tags[j][0] for j in ess),
    lottery_score1=int(m.group(1)) if m else 0,
)
json.dump(stats, open("results/provenance_stats.json", "w"), indent=1)


def last_epoch(path):
    rows = open(path).read().strip().split("\n")
    return int(rows[-1].split(",")[0]) if len(rows) > 1 else 0


def takeover_of(done):
    if not os.path.exists(done):
        return ""
    m = re.search(r"(\d+)", open(done).read())
    return m.group(1) if m else "none"


with open("results/replays.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["start_epoch", "replica", "epochs_run", "takeover"])
    for p in sorted(glob.glob("runs/replay/E*_r*.csv")):
        m = re.search(r"E(\d+)_r(\d+)", p)
        if not os.path.exists(p + ".done"):
            continue
        start = int(m.group(1))
        w.writerow([start, m.group(2), last_epoch(p) - start, takeover_of(p + ".done")])

with open("results/small_runs.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["n", "seed", "reached", "takeover", "finished"])
    for p in sorted(glob.glob("runs/small/n*_s*.csv"), key=lambda x: int(re.search(r"_s(\d+)", x).group(1))):
        m = re.search(r"n(\d+)_s(\d+)\.csv$", p)
        if not m:
            continue
        reached = last_epoch(p)
        if reached < 1000:
            continue
        w.writerow([m.group(1), m.group(2), reached, takeover_of(p + ".done"), int(os.path.exists(p + ".done"))])
print(json.dumps(stats, indent=1))
