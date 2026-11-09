# Replicators from noise

An experiment on the origin of self-replication in a "primordial soup" of
programs. The soup and language (BFF) come from
[Agüera y Arcas et al., *Computational Life* (2024)](https://arxiv.org/abs/2406.19108).
The simulator here is a fresh C/OpenMP reimplementation that follows the
authors' reference code ([cubff](https://github.com/paradigms-of-intelligence/cubff)),
plus one addition: a **provenance tag on every byte** recording when and how
its value was born (initial random soup, mutation, or a program's `+`/`-`
edit). Copy instructions carry tags with values.

The question: **is the first replicator a lucky draw, or the product of a
process?** The full write-up with charts and live demos is `web/index.html`
(open it in a browser).

## Findings

1. **Reproduced the transition.** In a soup of 131,072 programs (seed 1), nothing
   copies itself for 11,600 epochs. Then within about 130 epochs, more than 80% of
   programs replicate, and higher-order entropy jumps from 0.12 to 3.4 bits/byte.
2. **The mechanism is a reverse copy loop.** Knockouts show 3 essential
   instructions, `,` `}` `<` (copy head 1 to head 0, move them apart). The copy
   is mirrored, so a working replicator carries the motif and its mirror. A
   hand-built 10-byte replicator, `[,}<]` … `]<},[`, passes the replication test
   with any filler (196 of 200 random fillers).
3. **Not a uniform lottery.** 0 of 100,000,000 random programs replicate (95%
   bound: p < 3×10⁻⁸). The minimal motif has odds of about 256⁻¹⁰.
4. **Not a loaded-dice lottery either.** At the pre-life soup's own byte
   frequencies, the motif's odds are about 10⁻²⁰ per placement, about 10⁻¹⁴
   for the whole soup at any moment.
5. **But the soup is not primed.** Replays of seed 1 from five pre-life
   snapshots (epochs 8,960–11,520), each with new randomness: 0 takeovers in
   7,680 epochs. The original took over 256 epochs after the 11,520 snapshot.
   A full scan of that snapshot finds no program with any copying ability.
6. **Copying is not enough: R₀.** Against random partners, the minimal
   replicator makes 0.96 working copies per epoch and always dies out. The
   evolved one makes 1.16 and establishes about 1 time in 3. Going second is
   what kills replicators: the partner's code runs into them 78% of the time.
   The pre-life soup is no friendlier than noise (evolved R₀ ≈ 1.1 there).
   Measured directly, one evolved replicator took over 10 of 30 random
   soups and 5 of 30 pre-life soups (1,024 programs, 150 epochs). Every trial
   ended in extinction or near-total takeover.
7. **Everything gets rewritten.** By epoch 11,520, only 0.09% of bytes are
   original. 71% were last written by `+`/`-` and 29% by mutation. The dominant
   replicator's essential bytes trace to 3 birth events spread over
   about 1,600 epochs before takeover.

Interpretation: a process (the first few thousand epochs) turns noise into a
stationary pre-life soup that generates candidate code far faster than chance.
From then on, origin is a rare event of that stationary process: a working
copy loop must form *and* survive being rare (R₀ barely above 1).

## Layout

| path | what |
|---|---|
| `soup.c` | simulator: `run`, `analyze`, `lottery`, `score`, `exec` modes |
| `provenance.py` | summarise species provenance from `analyze -json` output |
| `collect.py` | turn raw runs/snapshots into `results/` files |
| `web/` | `bff.js` (JS interpreter, verified identical to C), page template and builder |
| `runs/*.sh` | batch scripts for full soups, replays, small soups |
| `results/` | logs and derived data used by the page |

## Reproduce

```sh
cc -O3 -march=native -fopenmp soup.c -o soup -lbrotlienc -lm
./runs/batch.sh 1                      # full soup, seed 1 (~1 h on 4 cores)
./soup lottery -samples 100000000 -thresh 16
./runs/replay.sh 1                     # needs runs/keep/ snapshots from seed 1
python3 collect.py && python3 web/build_page.py
```
