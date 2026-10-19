"""Shared helpers for the NickelReadingStats symbol tools."""

import re


def extract_symbols(source_path):
    """Return (hooks, dlsyms): the mangled `.sym` and `.name` names in the
    hook source, in order of appearance."""
    text = open(source_path, encoding="utf-8").read()
    hooks = re.findall(r'\.sym\s*=\s*"([^"]+)"', text)
    # `.name` is also used for the mod identifier ("NickelReadingStats");
    # only mangled (_Z...) names are dlsym targets.
    dlsyms = [n for n in re.findall(r'\.name\s*=\s*"([^"]+)"', text)
              if n.startswith("_Z")]
    return hooks, dlsyms
