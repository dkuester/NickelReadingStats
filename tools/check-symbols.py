#!/usr/bin/env python3
"""Verify NickelReadingStats hook/dlsym symbols against the target firmware.

Local dev tool — needs the target firmware (libnickel.so.1.0.0) and its
exported-symbol list (defsyms.txt), both extracted from the target 5.18+
firmware image (Tolino or Kobo).
Neither file is committed (proprietary); keep them in a local scratch dir.

Three checks:
1. Every `.sym` (hook) and `.name` (dlsym) mangled name in the source must
   exist in defsyms.txt. Catches typos and symbols that aren't in the firmware.
2. Every dlsym symbol is disassembled and its return convention is classified
   (sret-by-value vs. pointer/reference-return). A dlsym'd fn-ptr declared
   with the WRONG convention crashes nickel at runtime (the getMimeType bug:
   declared `QByteArray` by value, but the symbol returns `QByteArray&`).
3. Every hard-coded struct offset in the source (OFFSETS) matches the
   instruction that computes it in the firmware.

Usage:
    python3 tools/check-symbols.py \
        --source hook/src/readingstats.cc \
        --defsyms /path/to/defsyms.txt \
        --firmware /path/to/libnickel.so.1.0.0
"""

import argparse
import re
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

# Expected return convention per dlsym symbol (verified by disassembly).
#   "pointer" -> returns T&/T* (r0 = this, returns a pointer to a field)
#   "sret"    -> returns T by value via hidden r0 buffer (this lands in r1)
#   "simple"  -> returns bool/int/void (no sret, no pointer)
EXPECTED = {
    "_ZNK11ReadingView13isFixedLayoutEv": "simple",
    "_ZNK6Device14getDeviceClassEv": "simple",
    "_ZNK6Device24isParentalControlEnabledEv": "simple",
    "_ZNK7Content11getMimeTypeEv": "pointer",
    "_ZNK7Content5getIdERK7QString": "sret",
    "_ZN11ReadingView13gotWordCountsERK5QListIiE": "simple",
    "_ZN11ReadingView12chapterIndexEv": "simple",
    "_ZNK7Content16getAccessibilityEv": "simple",
}


def extract_symbols(source_path):
    """Return (hooks, dlsyms) — lists of mangled names from the source."""
    text = open(source_path, encoding="utf-8").read()
    hooks = re.findall(r'\.sym\s*=\s*"([^"]+)"', text)
    # `.name` is also used for the mod identifier ("NickelReadingStats");
    # only mangled (_Z...) names are dlsym targets.
    dlsyms = [n for n in re.findall(r'\.name\s*=\s*"([^"]+)"', text)
              if n.startswith("_Z")]
    return hooks, dlsyms


def parse_defsyms(path):
    """Return {mangled_name: address} from defsyms.txt."""
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) >= 3:
            out[p[2]] = int(p[0], 16)
    return out


def classify(data, addr, next_addr):
    """Classify the return convention of the function at VMA `addr`.

    The function body is [addr, next_addr) — the next symbol's address bounds
    it so we never bleed into the neighbouring function. Heuristics (Thumb):
      - `adds/add r0, #imm` immediately before the return -> returns a pointer
        (reference/pointer return, e.g. `QByteArray& getMimeType()`).
      - vtable chain from r1 (`ldr rX, [r1]` then deref rX) + r0 saved to a
        callee-saved reg -> sret (by-value return via hidden r0 buffer).
      - a load/store through r0 (or a reg r0 was copied to) -> r0 is `this`,
        i.e. no sret (scalar/void return).
    """
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    insns = list(md.disasm(data[addr:next_addr], addr))

    LOAD_STORE = ("ldr", "str", "ldrd", "strd", "ldrb", "strb", "ldrh", "strh")

    this_regs = {"r0"}
    r1_vtable = False
    tail_adds_r0 = False

    # Index of the final return (pop {.., pc} or bx lr).
    ret_i = None
    for i, ins in enumerate(insns):
        if ins.mnemonic == "pop" and "pc" in ins.op_str:
            ret_i = i
        elif ins.mnemonic == "bx" and "lr" in ins.op_str:
            ret_i = i

    for i, ins in enumerate(insns):
        ops = ins.op_str
        # Track copies of r0 (this-or-sret-buffer) into callee-saved regs.
        m = re.match(r"r([4-9]|1[01]), r0\b", ops)
        if ins.mnemonic == "mov" and m:
            this_regs.add("r" + m.group(1))
        if (ret_i is not None and ret_i - 2 <= i < ret_i
                and ins.mnemonic in ("adds", "add")
                and re.match(r"r0, .*#", ops)):
            tail_adds_r0 = True

    # r0 (or a copy of it) used as a memory base -> it is `this`, not an sret
    # buffer (sret buffers are passed onward, not indexed).
    r0_is_this = any(
        ins.mnemonic in LOAD_STORE and ("[%s" % r) in ins.op_str
        for ins in insns for r in this_regs)

    # vtable chain from r1: `ldr rX, [r1]` then a dereference of rX.
    # (distinguishes r1-as-`this` from r1-as-data-arg, e.g. a QList reference)
    for i in range(len(insns) - 1):
        m = re.match(r"r(\d+), \[r1\]$", insns[i].op_str)
        if insns[i].mnemonic == "ldr" and m:
            reg = "r" + m.group(1)
            for j in range(i + 1, min(i + 3, len(insns))):
                if insns[j].mnemonic == "ldr" and ("[%s," % reg) in insns[j].op_str:
                    r1_vtable = True

    if tail_adds_r0:
        return "pointer"
    if r1_vtable and len(this_regs) > 1:
        return "sret"
    if r0_is_this:
        return "simple"  # r0 = this, no obvious sret -> scalar/void
    return "unknown"


# Struct offsets the hook hard-codes, verified against the instruction that
# computes them inside a firmware function:
#   source constant -> (function symbol, regex on the op_str of its add)
OFFSETS = {
    "kReadingViewContentOffset": (
        "_ZN11ReadingView16updateWordCountsEv",
        r"r\d+, r0, #(0x[0-9a-f]+)$",
    ),
}


def check_offsets(source_text, defsyms, data, addrs):
    """Return the number of offset problems (printing one line per offset)."""
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    errors = 0
    for const, (sym, pattern) in sorted(OFFSETS.items()):
        m = re.search(r"\b%s\s*=\s*(0x[0-9a-fA-F]+|\d+)" % const, source_text)
        if not m:
            print("  MISS     %-28s not found in source" % const)
            errors += 1
            continue
        want = int(m.group(1), 0)
        if sym not in defsyms:
            print("  MISS     %-28s %s not in firmware" % (const, sym))
            errors += 1
            continue
        addr = defsyms[sym]
        next_addr = next((a for a in addrs if a > addr), addr + 0x120)
        got = None
        # The offset is taken from `this` near the top of the function.
        for ins in list(md.disasm(data[addr:next_addr], addr))[:24]:
            hit = re.match(pattern, ins.op_str)
            if ins.mnemonic in ("add", "add.w", "adds") and hit:
                got = int(hit.group(1), 16)
                break
        if got == want:
            print("  OK       %-28s 0x%x (%s)" % (const, want, sym))
        else:
            print("  MISMATCH %-28s source=0x%x firmware=%s (%s)" % (
                const, want, "?" if got is None else "0x%x" % got, sym))
            errors += 1
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True)
    ap.add_argument("--defsyms", required=True)
    ap.add_argument("--firmware", required=True)
    args = ap.parse_args()

    hooks, dlsyms = extract_symbols(args.source)
    symbols = {**{h: "hook" for h in hooks}, **{d: "dlsym" for d in dlsyms}}
    defsyms = parse_defsyms(args.defsyms)
    data = open(args.firmware, "rb").read()

    # Sorted addresses so classify() can bound each function body.
    addrs = sorted(defsyms.values())

    errors = 0

    print("=== symbol existence ===")
    for name, kind in sorted(symbols.items()):
        if name in defsyms:
            print("  OK   %-60s 0x%08x" % (name, defsyms[name]))
        else:
            print("  MISS %-60s (%s)" % (name, kind))
            errors += 1

    print("\n=== dlsym return convention ===")
    for name in sorted(dlsyms):
        if name not in defsyms:
            continue
        addr = defsyms[name]
        next_addr = next((a for a in addrs if a > addr), addr + 0x120)
        got = classify(data, addr, next_addr)
        want = EXPECTED.get(name)
        # A dlsym without a recorded expectation is an error too: otherwise a
        # new symbol silently skips the convention check that caught the
        # getMimeType crash. Disassemble it, then add it to EXPECTED.
        if want is None:
            status = "NOEXPECT"
        elif got == want:
            status = "OK"
        else:
            status = "MISMATCH"
        if status != "OK":
            errors += 1
        print("  %-8s %-60s got=%-8s want=%s" % (status, name, got, want or "?"))
        if got == "pointer":
            print("          ^ declared fn-ptr must return T&/T*, NOT T by value")

    print("\n=== hard-coded struct offsets ===")
    errors += check_offsets(open(args.source, encoding="utf-8").read(),
                            defsyms, data, addrs)

    print("\n%d hook(s), %d dlsym(s) checked, %d problem(s)." % (
        len(hooks), len(dlsyms), errors))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
