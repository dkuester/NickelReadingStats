#!/usr/bin/env python3
"""Verify NickelReadingStats hook/dlsym symbols against the target firmware.

Local dev tool — needs the target firmware (libnickel.so.1.0.0) and its
exported-symbol list (defsyms.txt), both extracted from the target 5.18+
firmware image (Tolino or Kobo).
Neither file is committed (proprietary); keep them in a local scratch dir.

Five checks:
1. Every `.sym` (hook) and `.name` (dlsym) mangled name in the source must
   exist in defsyms.txt. Catches typos and symbols that aren't in the firmware.
2. Every dlsym symbol is disassembled and its return convention is classified
   (sret-by-value vs. pointer/reference-return). A dlsym'd fn-ptr declared
   with the WRONG convention crashes nickel at runtime (the getMimeType bug:
   declared `QByteArray` by value, but the symbol returns `QByteArray&`).
3. Hooked functions whose return value the hook forwards (HOOK_RETURNS)
   return void/value as declared.
4. Class layouts the hook relies on (PRIMARY_BASES: ReadingView* used as
   QObject*) and the virtual calls of originals it mirrors (VIRTUAL_CALLS),
   resolved through typeinfo/vtable relocations.
5. Every hard-coded struct offset in the source (OFFSETS) matches the
   instruction that computes it in the firmware.

Usage:
    python3 tools/check-symbols.py \
        --source hook/src/readingstats.cc \
        --defsyms /path/to/defsyms.txt \
        --firmware /path/to/libnickel.so.1.0.0
"""

import argparse
import os
import re
import struct
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nhsyms import extract_symbols  # noqa: E402

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
    "_ZNK11ReadingView13hasOnlyImagesEv": "simple",
}


def parse_defsyms(path):
    """Return {mangled_name: address or None} from a symbol list.

    Accepts `nm`/`llvm-nm -D --defined-only` output ("addr type name") and a
    bare list of names (one per line, no addresses: existence check only).
    The Thumb bit is cleared, so odd st_values from other tools also work.
    """
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split()
        if len(p) >= 3:
            out[p[2]] = int(p[0], 16) & ~1
        elif len(p) == 1 and p[0].startswith("_Z"):
            out[p[0]] = None
    return out


class Firmware:
    """The firmware bytes plus a VMA -> file offset map from its PT_LOAD
    segments. Symbol addresses are VMAs; they equal file offsets only in a
    segment whose p_vaddr == p_offset, which is not guaranteed."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1:
            sys.exit("%s: not a 32-bit ELF file" % path)
        end = "<" if d[5] == 1 else ">"
        (phoff,) = struct.unpack_from(end + "I", d, 0x1C)
        phentsize, phnum = struct.unpack_from(end + "HH", d, 0x2A)
        self.end = end
        self.segments = []
        for i in range(phnum):
            p_type, p_offset, p_vaddr, _p_paddr, p_filesz = struct.unpack_from(
                end + "IIIII", d, phoff + i * phentsize)
            if p_type == 1:  # PT_LOAD
                self.segments.append((p_vaddr, p_offset, p_filesz))
        self._relocs = None

    def word(self, vma):
        """The 32-bit word stored at `vma` (before relocation)."""
        raw = self.read(vma, 4)
        return struct.unpack(self.end + "I", raw)[0] if len(raw) == 4 else None

    def relocations(self):
        """{vma: (type, symbol name or None)} from every SHT_REL section.
        Pointers in typeinfo/vtables are relocated at load time, so this is
        how a slot is resolved to the symbol it points at."""
        if self._relocs is not None:
            return self._relocs
        d, end = self.data, self.end
        (shoff,) = struct.unpack_from(end + "I", d, 0x20)
        shentsize, shnum = struct.unpack_from(end + "HH", d, 0x2E)
        sections = [struct.unpack_from(end + "IIIIIIIIII", d, shoff + i * shentsize)
                    for i in range(shnum)]
        # (name, type, flags, addr, offset, size, link, info, align, entsize)
        relocs = {}
        for sh in sections:
            if sh[1] != 9:  # SHT_REL
                continue
            symtab = sections[sh[6]]
            strtab = sections[symtab[6]]

            def symname(index):
                if index == 0:
                    return None
                st_name = struct.unpack_from(end + "I", d, symtab[4] + index * 16)[0]
                stop = d.index(b"\0", strtab[4] + st_name)
                return d[strtab[4] + st_name:stop].decode("ascii", "replace")

            for off in range(sh[4], sh[4] + sh[5], 8):
                r_offset, r_info = struct.unpack_from(end + "II", d, off)
                relocs[r_offset] = (r_info & 0xFF, symname(r_info >> 8))
        self._relocs = relocs
        return relocs

    def pointer(self, vma, by_address):
        """Resolve the pointer stored at `vma` to a symbol name: via its
        R_ARM_ABS32/GLOB_DAT symbol, or for R_ARM_RELATIVE via the stored
        address and `by_address` ({address: name}). None if unknown."""
        rel = self.relocations().get(vma)
        if rel is None:
            return None
        rtype, name = rel
        if name:
            return name
        if rtype == 23:  # R_ARM_RELATIVE: the stored word is the address
            target = self.word(vma)
            return by_address.get(target & ~1) if target is not None else None
        return None

    def read(self, vma, length):
        """Bytes at `vma` (clamped to its segment); empty if unmapped."""
        vma &= ~1  # Thumb bit
        for vaddr, offset, filesz in self.segments:
            if vaddr <= vma < vaddr + filesz:
                start = offset + vma - vaddr
                return self.data[start:start + min(length, vaddr + filesz - vma)]
        return b""


def classify(fw, addr, next_addr):
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
    insns = list(md.disasm(fw.read(addr, next_addr - addr), addr))

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


# Expected return kind of hooked functions whose return type matters (the
# hook forwards it). "void" = r0 is not written before any return, "value" =
# r0 is written right before a return. Return types are not in the mangled
# name; endReadingSession was once declared bool although it returns int.
HOOK_RETURNS = {
    "_ZN11EventEngine19startReadingSessionER6VolumeRK11PluginState4QMapI7QString8QVariantE": "void",
    "_ZN11EventEngine17endReadingSessionER6VolumeRK11PluginState4QMapI7QString8QVariantE": "value",
    "_ZN11EventEngine9fireEventEPKcRK6VolumeRK4QMapI7QString8QVariantE": "void",
    "_ZN11EventEngine13fireEventSlotE10QByteArray6Volume4QMapI7QString8QVariantE": "void",
}

R0_WRITERS = ("mov", "movs", "mov.w", "movw", "mvn", "mvns", "ldr", "ldr.w", "ldrb",
              "ldrh", "uxtb", "uxth", "sxtb", "sxth", "adds", "add", "add.w",
              "subs", "sub", "eor", "eors", "eor.w", "and", "ands", "orr", "orrs")


def classify_return(fw, addr, next_addr):
    """'value' if r0 is written within the 3 instructions before a return,
    'void' if returns exist and none of them is preceded by such a write,
    'unknown' if no return was found (e.g. the body ends in a tail call)."""
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    insns = list(md.disasm(fw.read(addr, next_addr - addr), addr))
    returns = [i for i, ins in enumerate(insns)
               if (ins.mnemonic.startswith("pop") and "pc" in ins.op_str)
               or (ins.mnemonic == "bx" and ins.op_str == "lr")]
    if not returns:
        return "unknown"
    for i in returns:
        for ins in insns[max(0, i - 3):i]:
            if ins.mnemonic in R0_WRITERS and re.match(r"r0\b", ins.op_str):
                return "value"
    return "void"


def check_hook_returns(defsyms, fw, addrs):
    errors = 0
    for sym, want in sorted(HOOK_RETURNS.items()):
        if sym not in defsyms:
            continue  # reported by the existence check
        addr = defsyms[sym]
        next_addr = next((a for a in addrs if a > addr), addr + 0x400)
        got = classify_return(fw, addr, next_addr)
        status = "OK" if got == want else "MISMATCH"
        if status != "OK":
            errors += 1
        print("  %-8s %-60s got=%-7s want=%s" % (status, sym[:60], got, want))
    return errors


# Class layouts the hook relies on: typeinfo -> required primary base. The
# hook treats a ReadingView* as its QObject* (QPointer in the word-count
# delivery), which holds only while QWidget is base[0] at offset 0.
PRIMARY_BASES = {
    "_ZTI11ReadingView": "_ZTI7QWidget",
}

# Virtual calls inside an original function the hook mirrors, as the set of
# methods they resolve to through the class vtable. If firmware adds, drops
# or changes one, the mirror in the hook is out of date.
VIRTUAL_CALLS = {
    "_ZNK11ReadingView20supportsReadingStatsEv": (
        "_ZTV11ReadingView", {"_ZNK11ReadingView13hasOnlyImagesEv"}),
}


def by_address(defsyms):
    out = {}
    for name, addr in sorted(defsyms.items()):
        if addr is not None:
            out.setdefault(addr, name)
    return out


def check_primary_bases(defsyms, fw):
    errors = 0
    names = by_address(defsyms)
    for ti, want in sorted(PRIMARY_BASES.items()):
        addr = defsyms.get(ti)
        if addr is None:
            print("  MISS     %-28s not in firmware" % ti)
            errors += 1
            continue
        kind = fw.pointer(addr, names) or "?"
        if "si_class_type_info" in kind:   # single base, offset 0 implied
            base, offset, public = fw.pointer(addr + 8, names), 0, True
        elif "vmi_class_type_info" in kind:
            base = fw.pointer(addr + 16, names)          # base[0] typeinfo
            flags = fw.word(addr + 20) or 0              # offset << 8 | flags
            offset, public = flags >> 8, bool(flags & 2)
        else:
            base, offset, public = None, None, False
        ok = base == want and offset == 0 and public
        if not ok:
            errors += 1
        print("  %-8s %-28s base[0]=%s offset=%s%s" % (
            "OK" if ok else "MISMATCH", ti, base, offset, "" if public else " (not public)"))
    return errors


def check_virtual_calls(defsyms, fw, addrs):
    """Find `ldr rV, [rT]` (vptr) followed by `ldr rF, [rV, #imm]` and a
    `blx rF` in the function, resolve each slot through the vtable, and
    compare the set of targets with VIRTUAL_CALLS."""
    errors = 0
    names = by_address(defsyms)
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    for func, (vtable, want) in sorted(VIRTUAL_CALLS.items()):
        addr, vt = defsyms.get(func), defsyms.get(vtable)
        if addr is None or vt is None:
            print("  MISS     %s / %s not in firmware" % (func[:40], vtable))
            errors += 1
            continue
        next_addr = next((a for a in addrs if a > addr), addr + 0x200)
        insns = list(md.disasm(fw.read(addr, next_addr - addr), addr))
        vptr_regs, slot_regs, got = set(), {}, set()
        for ins in insns:
            m = re.match(r"(r\d+|ip), \[(r\d+)\]$", ins.op_str)
            if ins.mnemonic in ("ldr", "ldr.w") and m:
                vptr_regs.add(m.group(1))
                continue
            m = re.match(r"(r\d+|ip), \[(r\d+), #(0x[0-9a-f]+|\d+)\]$", ins.op_str)
            if ins.mnemonic in ("ldr", "ldr.w") and m and m.group(2) in vptr_regs:
                slot_regs[m.group(1)] = int(m.group(3), 0)
                continue
            if ins.mnemonic == "blx" and ins.op_str in slot_regs:
                target = fw.pointer(vt + 8 + slot_regs[ins.op_str], names)
                got.add(target or "slot 0x%x" % slot_regs[ins.op_str])
        ok = got == want
        if not ok:
            errors += 1
        print("  %-8s %-44s calls %s" % ("OK" if ok else "MISMATCH", func[:44],
                                         ", ".join(sorted(got)) or "nothing"))
        if not ok:
            print("           expected %s" % ", ".join(sorted(want)))
    return errors


# Struct offsets the hook hard-codes, verified against the instruction that
# computes them inside a firmware function:
#   source constant -> (function symbol, regex on the op_str of its add)
OFFSETS = {
    "kReadingViewContentOffset": (
        "_ZN11ReadingView16updateWordCountsEv",
        r"r\d+, r0, #(0x[0-9a-f]+)$",
    ),
}


def check_offsets(source_text, defsyms, fw, addrs):
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
        for ins in list(md.disasm(fw.read(addr, next_addr - addr), addr))[:24]:
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
    ap.add_argument("--defsyms", required=True,
                    help="llvm-nm -D --defined-only output, or a bare name list "
                         "(existence check only)")
    ap.add_argument("--firmware",
                    help="libnickel.so.1.0.0; without it only existence is checked")
    args = ap.parse_args()

    hooks, dlsyms = extract_symbols(args.source)
    symbols = {**{h: "hook" for h in hooks}, **{d: "dlsym" for d in dlsyms}}
    defsyms = parse_defsyms(args.defsyms)

    errors = 0

    print("=== symbol existence ===")
    for name, kind in sorted(symbols.items()):
        if name not in defsyms:
            print("  MISS %-60s (%s)" % (name, kind))
            errors += 1
        elif defsyms[name] is None:
            print("  OK   %-60s (no address)" % name)
        else:
            print("  OK   %-60s 0x%08x" % (name, defsyms[name]))

    # The disassembly checks need addresses and the binary.
    have_addrs = all(defsyms.get(n) is not None for n in symbols if n in defsyms)
    if not (args.firmware and have_addrs):
        print("\nPARTIAL: existence only. For the convention, return-kind and "
              "offset checks pass --firmware and a defsyms list with addresses "
              "(llvm-nm -D --defined-only libnickel.so.1.0.0).")
        print("%d hook(s), %d dlsym(s) checked, %d problem(s)." % (
            len(hooks), len(dlsyms), errors))
        sys.exit(1 if errors else 0)

    fw = Firmware(args.firmware)
    # Sorted addresses so each function body can be bounded by the next symbol.
    addrs = sorted({a for a in defsyms.values() if a is not None})

    print("\n=== dlsym return convention ===")
    for name in sorted(dlsyms):
        if name not in defsyms:
            continue
        addr = defsyms[name]
        next_addr = next((a for a in addrs if a > addr), addr + 0x120)
        got = classify(fw, addr, next_addr)
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

    print("\n=== hook return kind (void vs. value) ===")
    errors += check_hook_returns(defsyms, fw, addrs)

    print("\n=== class layout (primary bases) ===")
    errors += check_primary_bases(defsyms, fw)

    print("\n=== virtual calls in mirrored originals ===")
    errors += check_virtual_calls(defsyms, fw, addrs)

    print("\n=== hard-coded struct offsets ===")
    errors += check_offsets(open(args.source, encoding="utf-8").read(),
                            defsyms, fw, addrs)

    print("\n%d hook(s), %d dlsym(s) checked, %d problem(s)." % (
        len(hooks), len(dlsyms), errors))
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
