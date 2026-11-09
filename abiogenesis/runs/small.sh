#!/bin/bash
# Many independent small soups (single-threaded) to measure the distribution
# of waiting times to takeover. Args: N  CAP  seeds...
cd "$(dirname "$0")/.."
N=$1; CAP=$2; shift 2
for s in "$@"; do
  out=runs/small/n${N}_s$s.csv
  [ -s $out.done ] && continue
  OMP_NUM_THREADS=1 ./soup run -n $N -seed $s -epochs $CAP -log 32 -sample 256 -stop-after 0 > $out 2> $out.err
  echo "$(grep -o 'takeover at epoch [0-9]*' $out.err || echo none)" > $out.done
done
