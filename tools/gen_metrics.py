#!/usr/bin/env python3
"""Regenerates src/metrics_data.inc from Pele's src/text/metrics.ts."""
import re, sys
src = open(sys.argv[1] if len(sys.argv) > 1 else '/home/claude/pele/src/text/metrics.ts').read()
reg = re.search(r"REGULAR =\s*'([0-9a-z]+)'", src).group(1)
bold = re.search(r"BOLD =\s*'([0-9a-z]+)'", src).group(1)
print(f'inline constexpr char kRegularMetrics[] = "{reg}";')
print(f'inline constexpr char kBoldMetrics[] = "{bold}";')
