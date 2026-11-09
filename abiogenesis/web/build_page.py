"""Build web/index.html from template.html plus experiment data.

Reads: results/s1_log.csv, results/s1_11776.json, results/lottery.txt,
       results/replays.csv, results/small_runs.csv, results/hero.json,
       results/provenance_stats.json
"""
import csv
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "results")


def rd(name):
    return os.path.join(RES, name)


def fmt(n):
    return f"{n:,}"


# --- seed-1 log -------------------------------------------------------------
log = []
takeover = None
with open(rd("s1_log.csv")) as f:
    for r in csv.DictReader(f):
        e = int(r["epoch"])
        frac = float(r["sampled_replicators"])
        log.append([e, round(float(r["higher_entropy"]), 4), round(frac, 4)])
        if takeover is None and frac >= 0.5:
            takeover = e

# --- dominant replicator ----------------------------------------------------
d = json.load(open(rd("s1_11776.json")))
sp = d["species"][0]
ess = [j for j, k in enumerate(sp["knockout"]) if k < sp["score"] / 2]
evolved = dict(hex=sp["hex"], essential=ess, count=sp["count"], score=sp["score"],
               epoch=d["epoch"], replicators=d["replicators"])

# --- lottery ----------------------------------------------------------------
lot = open(rd("lottery.txt")).read().split()
samples = int(lot[lot.index("samples") + 1])
hits = int(lot[lot.index("replicators(score>=16)") + 1])
# 95% upper bound for zero events (rule of three)
p_upper = 3.0 / samples

prov = json.load(open(rd("provenance_stats.json")))
hero = json.load(open(rd("hero.json")))

# --- replays ----------------------------------------------------------------
replays = list(csv.DictReader(open(rd("replays.csv"))))
rep_epochs = sum(int(r["epochs_run"]) for r in replays)
rep_hits = sum(1 for r in replays if r["takeover"] not in ("", "none"))

# --- small soups ------------------------------------------------------------
small = list(csv.DictReader(open(rd("small_runs.csv"))))
waits = {"cap": 0, "runs": []}
for r in small:
    t = None if r["takeover"] in ("", "none") else int(r["takeover"])
    reached = int(r["reached"])
    waits["runs"].append({"name": f"seed {r['seed']}", "takeover": t, "reached": reached})
    waits["cap"] = max(waits["cap"], reached)
waits["cap"] = int(math.ceil(max(waits["cap"], 1) / 10000.0) * 10000)
small_hits = [r for r in waits["runs"] if r["takeover"] is not None]
small_epochs = sum((r["takeover"] or r["reached"]) for r in waits["runs"])

interactions_s1 = takeover * (131072 // 2)

p1 = (f"I generated {fmt(samples)} uniformly random 64-byte programs and ran the same replication test on each. "
      f"{'None' if hits == 0 else fmt(hits)} passed. Only {prov['lottery_score1']} copied even a single byte reliably. "
      f"So fewer than about 3 in {fmt(samples)} random programs replicate (95% bound), and a fresh soup of 131,072 programs "
      f"contains one less than {131072 * p_upper * 100:.1f}% of the time. For the minimal replicator, the odds are "
      f"about 1 in 256<sup>10</sup>, roughly 10<sup>-24</sup> per placement.")

p2 = (f"The pre-life soup isn't uniform. By epoch 11,520 the commonest byte is <code>&lt;</code> at {prov['freq_lt']*100:.1f}%, "
      f"and <code>,</code> makes up {prov['freq_comma']*100:.1f}%. Drawing the ten bytes independently at the soup's own frequencies "
      f"gives about {prov['p_loaded']:.0e} per placement. Counting every program and about ten workable variants, the soup "
      f"should hold a complete motif about 10<sup>{round(math.log10(prov['p_loaded'] * 131072 * 10))}</sup> of the time. "
      f"Even if the whole soup were redrawn from scratch every epoch, {fmt(takeover)} epochs would give odds near "
      f"10<sup>{round(math.log10(prov['p_loaded'] * 131072 * 10 * takeover))}</sup>. Yet seed 1 got there in exactly that time. "
      f"Fragments are around: {prov['frag']} programs carry <code>,}}&lt;</code> or its mirror, but none has the full motif.")

p3 = (f"If the pre-life soup were steadily building toward a replicator, a snapshot taken just before takeover should be close to the edge. "
      f"I restarted seed 1 from five snapshots (epochs 8,960 to 11,520), each with fresh randomness, and ran each for 1,536 epochs. "
      f"{'None' if rep_hits == 0 else fmt(rep_hits)} of {len(replays)} replays took over, across {fmt(rep_epochs)} epochs in total. "
      f"The original run took over 256 epochs after its 11,520 snapshot. The replay from that same state ran six times longer "
      f"without doing so. At epoch 11,520 the soup was not on the brink. A full scan of it found zero programs with any copying score.")

if small_hits:
    hit_txt = ", ".join(fmt(r["takeover"]) for r in small_hits)
    p4_hits = f"{len(small_hits)} of {len(waits['runs'])} took over (at epochs {hit_txt})."
else:
    p4_hits = f"None of the {len(waits['runs'])} took over."
p4 = (f"A rare, memoryless event should have an exponential waiting time, and its rate should scale with the number of pairings. "
      f"I ran independent soups with 16,384 programs, one eighth the size, on one CPU core each. {p4_hits} "
      f"Together they ran {fmt(small_epochs)} epochs. If the rate per pairing matches seed 1's, a soup this size should need about "
      f"{fmt(interactions_s1 // 8192)} epochs on average. If the rate were per epoch, independent of soup size, these runs "
      f"should already have taken over several times.")

waitcap = ("Each line is an independent 16,384-program soup. A dot marks takeover. An arrowhead means the run was stopped with "
           "no takeover (censored). These runs used one core each, which is why there are few of them.")

prov_txt = (f"By the time replicators appear, the soup has been rewritten almost completely. At epoch 11,520 only "
            f"{prov['init_frac']*100:.2f}% of bytes still hold their original random values. {prov['edit_frac']*100:.0f}% were last "
            f"written by a <code>+</code> or <code>-</code> instruction, and {prov['mut_frac']*100:.0f}% came from mutations. "
            f"The median byte is about {fmt(prov['median_age'])} epochs old. The dominant replicator matches that background: "
            f"{prov['rep_edit']} of its 64 bytes are edit-born and {prov['rep_mut']} are mutation-born, with none left from the initial soup. "
            f"Its six essential bytes trace back to {prov['ess_events']} separate birth events between epochs "
            f"{fmt(prov['ess_min'])} and {fmt(prov['ess_max'])}. So the copy loop's parts existed, scattered, "
            f"for up to {fmt(takeover - prov['ess_min'])} epochs before they came together.")

conclusion = (
    "<p class='verdict'>Neither story alone fits. The first replicator is far too improbable to be a lucky draw, "
    "yet once the soup reaches its pre-life state, the moment it appears behaves like a lottery.</p>"
    "<p>My reading: the early soup is a process; the moment of origin is a lottery. In the first few thousand epochs the soup "
    "changes fast. Instruction counts per pairing rise from 65 to about 230, single-byte entropy falls, and nearly every original byte is "
    "overwritten by computation. Then it settles into a stationary pre-life regime that shuffles, copies and edits fragments far "
    "faster than chance would produce them. The replicator is a rare outcome of that regime: the soup prints its own lottery "
    "tickets, by the billion, and they are far better tickets than random bytes. A winning ticket also has to be cashed: with R<sub>0</sub> "
    "barely above 1, a new replicator usually dies out before anyone would notice it. No snapshot I looked at was primed. A state 256 epochs "
    "before takeover was as far from the edge as one 2,800 epochs before.</p>"
    "<p>This fits the follow-up paper “BFF: Simple explanations for complex phenomena” (arXiv 2607.01483), which reportedly finds "
    "replicators faster with pure random search plus tuned mutation than with pairwise interaction. I could not read that paper from my "
    "sandbox, so the agreement is with its abstract, not its methods.</p>")

caveats = [
    "One full-size soup transitioned (seed 1). The replay and small-soup results are informative but small samples. The waiting-time "
    "claim in particular needs more runs than four CPU cores allow in a session.",
    "My simulator reimplements the paper's reference code (cubff) from its source. I didn't run cubff itself, so small differences are possible. "
    "One I know of: my random number streams differ, so runs are not bit-identical to the paper's.",
    "“Replicator” means a self-replication score of at least 32 out of 64 in a test ported from cubff: run the program against random "
    "partners for five generations and count positions that come out consistently in both halves.",
    "An “edit” tag means the byte's value was last changed by <code>+</code> or <code>-</code>. A single increment of a random byte counts as an edit.",
    "Higher-order entropy uses Brotli at quality 2 with a 2<sup>24</sup> window, matching the reference code.",
    "Code, logs and analysis are in the FreeWill repository under <code>abiogenesis/</code>. The simulator is <code>soup.c</code>.",
]

facts = [
    [fmt(takeover), "epoch when replicators took over seed 1, out of 16,384 allowed"],
    ["~130", "epochs from the first replicator to 80% of the soup"],
    ["0", f"replicators among {samples/1e6:.0f} million random programs"],
    ["10 bytes", "the smallest replicator I found: a 3-instruction copy loop and its mirror"],
]

r0 = json.load(open(rd("r0.json")))
r0rows = []
for k, v in r0.items():
    who, env = k.split(" in ")
    r0rows.append(dict(who=who, env=env.replace("e8960", "(epoch 8,960)").replace("e11520", "(epoch 11,520)"), **v))
est = json.load(open(rd("establishment.json")))
er, ep = est["random"], est["pre-life"]
r0text = ("Columns count working replicators after one pairing (the program itself plus its child), averaged over 1,500 trials each. "
          "In a branching process, a lineage with R<sub>0</sub> below 1 always dies out. Just above 1, most lineages still die out by "
          "chance while they're rare. I tested that directly: one evolved replicator dropped into a 1,024-program soup, run for 150 epochs. "
          f"It took over {er['established']} of {er['trials']} random soups and {ep['established']} of {ep['trials']} soups built from "
          "seed 1's pre-life programs. Every trial ended in either extinction or near-total takeover, nothing in between. The pre-life soup is no "
          "friendlier than noise; if anything it is harsher. So the origin of replication here has two hurdles. A working copy loop has "
          "to form, and then it has to survive being rare. Many may form and vanish before one sticks.")
data = dict(r0=dict(rows=r0rows, text=r0text), log=log, takeover=takeover, evolved=evolved, hero=hero, facts=facts,
            lottery=dict(p1=p1, p2=p2, p3=p3, p4=p4, waitcap=waitcap),
            waits=waits, prov=prov_txt, conclusion=conclusion, caveats=caveats)

tpl = open(os.path.join(HERE, "template.html")).read()
bff = open(os.path.join(HERE, "bff.js")).read()
html = tpl.replace("/*DATA*/null", json.dumps(data, separators=(",", ":"))).replace("/*BFFJS*/", bff)
open(os.path.join(HERE, "index.html"), "w").write(html)
print("wrote web/index.html", len(html), "bytes")
