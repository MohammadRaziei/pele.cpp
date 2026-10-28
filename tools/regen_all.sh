#!/bin/bash
# Rebuilds every file in tests/data from the TypeScript Pele that is checked out at $PELE_TS
# (default /home/claude/pele, where the tools expect it; its dist/ must be built).
#
#   tools/regen_all.sh           rebuild tests/data in place (after moving to a new Pele version)
#   tools/regen_all.sh --check   rebuild, report which files differ from the committed ones, and put
#                                the committed ones back. On the version the data was made with,
#                                nothing should differ: that proves the arguments below are right.
#
# The seed and the count of each set are the ones the committed files were made with, except for the
# sets in OLD below, which were made in the first sessions with arguments that were not written down
# (their counts here match their size, with seed 1). --check cannot hold those to the committed
# files, so it skips them; everything else must come out byte for byte the same.
OLD="compound_cases.txt doc_cases.txt edge_cases.txt front_cases.txt label_cases.txt layered_cases.txt lexer_cases.txt parser_cases.txt preprocess_cases.txt shape_cases.txt theme_cases.txt url_cases.txt"
set -e
cd "$(dirname "$0")/.."
PELE_TS=${PELE_TS:-/home/claude/pele}
[ -d "$PELE_TS/dist" ] || { echo "no $PELE_TS/dist: build Pele first (npm ci && npm run build)"; exit 1; }
[ "$PELE_TS" = /home/claude/pele ] || { echo "the tools read /home/claude/pele; link it: ln -sfn $PELE_TS /home/claude/pele"; exit 1; }

D=tests/data
check=0
if [ "$1" = "--check" ]; then
  check=1
  rm -rf /tmp/pele_data_backup && cp -r $D /tmp/pele_data_backup
fi

W=/tmp/pele_regen_dirs
rm -rf $W && mkdir -p $W

# Unit-level sets: node tools/<tool> <seed> <count> <out>
unit() { node tools/gen_$1_cases.mjs "${@:2}" > /dev/null; }
unit compound 1 400 $D/compound_cases.txt
unit doc 1 1200 $D/doc_cases.txt
unit edge 1 1500 $D/edge_cases.txt
unit front 1 1800 $D/front_cases.txt
unit label 1 600 $D/label_cases.txt
unit layered 1 400 $D/layered_cases.txt
unit lexer 1 1500 $D/lexer_cases.txt
unit parser 1 1200 $D/parser_cases.txt
unit preprocess 1 800 $D/preprocess_cases.txt
unit svg 4 1600 $D/svg_cases.txt
unit theme 1 2000 $D/theme_cases.txt
unit url 1 1500 $D/url_cases.txt
unit shape 1 $D/shape_cases.txt
unit format 1 1500 $D/format_cases.txt
unit text 1 1500 $D/text_cases.txt
unit tokens 1 120 $D/token_tables.txt $D/token_cases.txt
unit scanner 1 1000 $D/scanner_cases.txt
unit outline 1 1500 $D/outline_cases.txt
unit seed 1 1500 $D/seed_cases.txt
unit members 1 1000 $D/members_cases.txt
unit class_lexer 1 1500 $D/class_lexer_cases.txt
unit class_model 1 600 $D/class_model_cases.txt
# Every regular expression in Pele's sources, and strings they are tried on
node tools/extract_regexes.mjs /tmp/regexes.json > /dev/null
node tools/gen_regex_cases.mjs /tmp/regexes.json 1 40 $D/regex_cases.txt > /dev/null

# End-to-end sets: random diagrams rendered by pele.js, packed with the SHA-256 of each SVG.
e2e() {  # name generator count
  node tools/$2 1 $3 $W/$1 > /dev/null
  node tools/pack_e2e.mjs $W/$1 $D/e2e_$1.txt > /dev/null
}
node tools/gen_e2e_cases.mjs 1 150 $W/flowchart > /dev/null
node tools/pack_e2e.mjs $W/flowchart $D/e2e.txt > /dev/null
e2e info gen_info_cases.mjs 300
e2e packet gen_packet_cases.mjs 400
e2e kanban gen_kanban_cases.mjs 600
e2e mindmap gen_mindmap_cases.mjs 500
e2e pie gen_pie_cases.mjs 500
e2e treemap gen_treemap_cases.mjs 500
e2e cynefin gen_cynefin_cases.mjs 500
e2e class gen_class_cases.mjs 600

if [ $check = 1 ]; then
  differ=0
  for f in $D/*; do
    n=$(basename $f)
    case " $OLD " in *" $n "*) continue;; esac
    if ! cmp -s $f /tmp/pele_data_backup/$n; then echo "DIFFERS: $n"; differ=$((differ+1)); fi
  done
  rm -rf $D && cp -r /tmp/pele_data_backup $D
  echo "$differ file(s) differ from the committed data (the committed data is back in place)"
  [ $differ = 0 ]
fi
