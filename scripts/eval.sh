#!/usr/bin/env bash
# Runs DevPilot on every bug in examples/bugs and prints a results table.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/devpilot"
pass=0
total=0

printf "%-28s %-7s %-7s %-14s %-6s\n" "BUG" "RESULT" "BUILDS" "TOKENS(in/out)" "SECS"
printf -- "-----------------------------------------------------------------------\n"

for dir in "$ROOT"/examples/bugs/*/; do
  name="$(basename "$dir")"
  tmp="$(mktemp -d)"
  cp -r "$dir". "$tmp"          # always run on a copy

  start=$(date +%s)
  out="$("$BIN" "$tmp" 2>&1)"
  secs=$(( $(date +%s) - start ))

  builds=$(echo "$out" | grep -c "=== Iteration")
  tokens=$(echo "$out" | grep -o "tokens in/out: [0-9]*/[0-9]*" | sed 's|tokens in/out: ||')
  [ -z "$tokens" ] && tokens="-"

  if echo "$out" | grep -q "BUILD OK"; then res="PASS"; pass=$((pass+1)); else res="FAIL"; fi
  total=$((total+1))

  printf "%-28s %-7s %-7s %-14s %-6s\n" "$name" "$res" "$builds" "$tokens" "$secs"
  rm -rf "$tmp"
  sleep 4                         # stay under free-tier rate limits
done

printf -- "-----------------------------------------------------------------------\n"
echo "Fixed $pass of $total bugs"