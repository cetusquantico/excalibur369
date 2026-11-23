#!/usr/bin/env bash
# EXCALIBUR369 benchmark harness.
#
#   run_bench.sh <label> [-- extra kh71 args...]
#
# Runs the SAME workload REPS times and reports the MEDIAN, because single runs
# on a loaded desktop vary by ~10%.  Never quote a speedup from one sample.
set -euo pipefail
BIN="${KH71:-./build/kh71}"
LABEL="${1:-unlabeled}"; shift || true
[ "${1:-}" = "--" ] && shift || true
REPS="${BENCH_REPS:-5}"
LIMIT="${BENCH_LIMIT:-6000000}"
THREADS="${BENCH_THREADS:-4}"
TARGET=0000deadbeef0000deadbeef0000deadbeef0000
if ! [[ "$REPS" =~ ^[1-9][0-9]*$ ]]; then
  echo "BENCH_REPS must be a positive integer" >&2
  exit 2
fi
sample_file=$(mktemp "${TMPDIR:-/tmp}/ex369-bench.XXXXXX")
trap 'rm -f "$sample_file"' EXIT

vals=()
for i in $(seq 1 "$REPS"); do
  v=$("$BIN" --puzzle 71 --hash160 "$TARGET" --threads "$THREADS" \
             --limit "$LIMIT" --affinity "${BENCH_AFFINITY:-on}" --quiet "$@" \
        | awk '/CANDIDATES\/s/ {print $3, $4}')
  n=$(echo "$v" | awk '{r=$1; if ($2=="M") r*=1e6; else if ($2=="K") r*=1e3; else if ($2=="G") r*=1e9; print r}')
  vals+=("$n")
done
printf '%s\n' "${vals[@]}" | sort -n > "$sample_file"
med=$(awk '{a[NR]=$1} END {print (NR%2) ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2}' "$sample_file")
lo=$(head -1 "$sample_file"); hi=$(tail -1 "$sample_file")
awk -v l="$LABEL" -v m="$med" -v lo="$lo" -v hi="$hi" -v r="$REPS" 'BEGIN{
  printf "%-34s median %7.3f M cand/s   (n=%d, range %.3f-%.3f M)\n", l, m/1e6, r, lo/1e6, hi/1e6
}'
echo "$med"> /dev/null
