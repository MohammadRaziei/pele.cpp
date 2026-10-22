#!/bin/sh
# Renders every N.mmd in a directory with pele-cli and compares with the SVG pele.js made (N.svg).
dir=${1:-/tmp/e2e}
cli=${2:-./build/pele-cli}
ok=0; bad=0; errmatch=0; shown=0
for f in "$dir"/*.mmd; do
  n=${f%.mmd}
  if [ -f "$n.svg" ]; then
    "$cli" "$f" > "$n.cpp.svg" 2> "$n.cpp.err"
    if [ "$(cat "$n.svg")" = "$(cat "$n.cpp.svg")" ]; then ok=$((ok+1)); else bad=$((bad+1)); [ $shown -lt 5 ] && echo "DIFF: $f" && shown=$((shown+1)); fi
  else
    "$cli" "$f" > /dev/null 2> "$n.cpp.err"
    if [ $? -ne 0 ]; then errmatch=$((errmatch+1)); else bad=$((bad+1)); [ $shown -lt 5 ] && echo "SHOULD FAIL: $f" && shown=$((shown+1)); fi
  fi
done
echo "svg identical: $ok, error cases agree: $errmatch, different: $bad"
