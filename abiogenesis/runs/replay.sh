#!/bin/bash
# Replay experiment: restart seed-1's pre-life soup from snapshots taken at
# different times before its takeover (epoch ~11.7k), each time with fresh
# randomness, and record how long until replicators take over (>=50% of a
# 256-tape sample), capped at CAP epochs.
cd "$(dirname "$0")/.."
CAP=${CAP:-1536}
mkdir -p runs/replay
for r in "$@"; do
  for E in 11520 11264 10752 9984 8960; do
    out=runs/replay/E${E}_r$r.csv
    [ -s $out.done ] && continue
    ./soup run -n 131072 -load runs/keep/s1_$E.bin -seed $((1000 + r)) -epochs $CAP -log 16 \
      -sample 256 -stop-after 0 > $out 2> $out.err
    echo "$(grep -o 'takeover at epoch [0-9]*' $out.err || echo none)" > $out.done
  done
done
