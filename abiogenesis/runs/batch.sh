#!/bin/bash
# Full-size soups (2^17 tapes, paper defaults). Snapshots every 256 epochs;
# stop 1024 epochs after takeover (>=50% of sampled tapes replicate). A pruner
# keeps only the last 12 snapshots so disk use stays bounded (~0.9 GB/run).
cd "$(dirname "$0")/.."
for seed in "$@"; do
  d=runs/s$seed; mkdir -p $d
  ( while sleep 20; do ls $d/snap_*.bin 2>/dev/null | head -n -12 | xargs -r rm -f; done ) &
  pruner=$!
  ./soup run -n 131072 -seed $seed -epochs 16384 -log 32 -sample 512 -snap 256 -dir $d -stop-after 1024 \
    > $d/log.csv 2> $d/stderr.txt
  kill $pruner
  ls $d/snap_*.bin 2>/dev/null | head -n -12 | xargs -r rm -f
done
