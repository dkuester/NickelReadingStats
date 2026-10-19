#!/usr/bin/env python3
"""CI-friendly symbol-manifest check (no firmware needed).

Extracts every `.sym` (hook) and `.name` (dlsym) mangled symbol from the hook
source and asserts the set matches tools/symbols.txt exactly. A mismatch means
a symbol was added, removed, or typo'd without updating the manifest — which is
only valid after running tools/check-symbols.py against the real firmware.

Usage:
    python3 tools/check-manifest.py \
        --source hook/src/readingstats.cc \
        --manifest tools/symbols.txt
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nhsyms import extract_symbols  # noqa: E402


def extract(source_path):
    hooks, dlsyms = extract_symbols(source_path)
    return set(hooks) | set(dlsyms)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True)
    ap.add_argument("--manifest", required=True)
    args = ap.parse_args()

    src = extract(args.source)
    man = {line.strip() for line in open(args.manifest, encoding="utf-8")
           if line.strip() and not line.startswith("#")}

    only_src = src - man
    only_man = man - src

    if only_src or only_man:
        for s in sorted(only_src):
            print("in source, not in manifest: %s" % s)
        for s in sorted(only_man):
            print("in manifest, not in source: %s" % s)
        print("Run tools/check-symbols.py against the firmware, then update "
              "tools/symbols.txt.")
        sys.exit(1)

    print("OK: %d symbol(s) match the manifest." % len(src))
    sys.exit(0)


if __name__ == "__main__":
    main()
