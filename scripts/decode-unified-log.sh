#!/usr/bin/env bash
# Decode a macOS unified log copied by `linux-collect-logs.sh --unified` into plain text, no sudo, no macOS.
#
#   bash scripts/decode-unified-log.sh ~/ga403-logs/<time>/macos/logarchive "2026-10-10 14:57:00" [out.txt]
#
# SINCE is UTC (OpenCore logs "Boot timestamp" in UTC; macOS shows local time, here PDT = UTC-7).
# Output: one line per entry "HH:MM:SS process[pid] message", time-ordered per source file, long messages cut at 220 chars.
# Parser: mandiant/macos-unifiedlogs (example `unifiedlog_iterator`), built once into ~/.cache/ga403 (needs cargo + network
# for crates on the first build). The archive layout it wants has uuidtext/* and diagnostics/* flattened into one directory,
# which this script does in place on first use.
set -eu
AR=${1:?logarchive dir}; SINCE=${2:?UTC time, e.g. "2026-10-10 14:57:00"}; OUT=${3:-$AR/../decoded.txt}
SRC=${SRC:-$HOME/.cache/ga403/macos-unifiedlogs}
BIN=$SRC/examples/target/release/unifiedlog_iterator
if [ ! -x "$BIN" ]; then
  [ -d "$SRC" ] || git clone --depth 1 https://github.com/mandiant/macos-unifiedlogs "$SRC"
  (cd "$SRC/examples/unifiedlog_iterator" && cargo build --release)
fi
if [ -d "$AR/diagnostics" ] && [ -d "$AR/uuidtext" ]; then mv "$AR"/diagnostics/* "$AR"/uuidtext/* "$AR"/ && rmdir "$AR/diagnostics" "$AR/uuidtext"; fi
T=$(date -u -d "$SINCE" +%s)
TMP=$(mktemp); trap 'rm -f "$TMP"' EXIT      # the full archive decodes to ~10 GB of JSON lines
"$BIN" -m log-archive -i "$AR" -o "$TMP" -f jsonl 2>/dev/null || true
jq -r --argjson t "${T}000000000" 'select(.time >= $t) |
  "\(.time/1e9|todate|.[11:19]) \(.process//"?"|split("/")|last)[\(.pid)] \((.message//"")|gsub("\n";" ")|.[0:220])"' "$TMP" | sort -s -k1,1 > "$OUT"
echo "$(wc -l < "$OUT") lines -> $OUT"
