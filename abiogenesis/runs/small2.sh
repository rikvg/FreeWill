#!/bin/bash
# Like small.sh, but keeps rolling snapshots (every 32 epochs, last 24 kept)
# so the first replicators can be dissected. Args: N CAP seeds...
cd "$(dirname "$0")/.."
N=$1; CAP=$2; shift 2
for s in "$@"; do
  out=runs/small/n${N}_s$s.csv; d=runs/small/n${N}_s${s}_snaps; mkdir -p $d
  [ -s $out.done ] && continue
  ( while sleep 10; do ls $d/snap_*.bin 2>/dev/null | head -n -24 | xargs -r rm -f; done ) & pr=$!
  OMP_NUM_THREADS=1 ./soup run -n $N -seed $s -epochs $CAP -log 32 -sample 256 -snap 32 -dir $d \
    -stop-after 64 > $out 2> $out.err
  kill $pr; ls $d/snap_*.bin 2>/dev/null | head -n -24 | xargs -r rm -f
  echo "$(grep -o 'takeover at epoch [0-9]*' $out.err || echo none)" > $out.done
done
