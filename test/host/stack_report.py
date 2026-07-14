#!/usr/bin/env python3
"""
stack_report.py -- worst-case stack bound for the DroidNet AVR contract forks.

Computes a static upper bound on RAM stack consumption directly from the LINKED
firmware.elf, then adds the linker's static .data+.bss figure to answer the only
question that matters on an ATmega32U4: does it fit in 2,560 B of SRAM?

A stack overflow on AVR does not trap. It walks down into .bss and silently corrupts
globals. This tool exists so that failure mode is a number in CI, not a mystery on a
bench.

-----------------------------------------------------------------------------------
WHY NOT -fstack-usage
-----------------------------------------------------------------------------------
These builds compile with -flto. Under LTO the compile step emits GIMPLE, not machine
code, so gcc has no frame to report and writes an EMPTY .su file for every LTO'd
translation unit (verified: src/main.cpp.su is 0 bytes). A tool that trusts .su here
reports a stack usage of ZERO for the entire application and looks like it worked.
The linked ELF is the only ground truth -- and it has the side benefit of covering
libgcc, avr-libc and the Arduino core, none of which ship .su files at all.

-----------------------------------------------------------------------------------
HOW IT WORKS
-----------------------------------------------------------------------------------
Two layers:

1. ADDRESS-LEVEL CFG WALK, per function entry. Follows fallthrough and every branch,
   tracking the SP delta instruction by instruction, stopping at ret/reti. This is what
   makes the softfloat library tractable: __divsf3 / __divsf3x / __divsf3_pse /
   __fp_splitA / __fp_round ... are NOTYPE, size-0 labels that FALL THROUGH into one
   another and rjmp between themselves. Any tool that models them as separate symbols
   with a naive call graph either invents recursion that isn't there or misses their
   pushes entirely.

2. SYMBOL-LEVEL CALL GRAPH on top, memoised, with real cycle detection.

SP-delta model:
   push/pop                          +/- 1
   jmp __prologue_saves__+K          -mcall-prologues. See below.
   in r28,SPL / in r29,SPH
     + sbiw r28,N | subi/sbci r28,N  frame of N bytes (the Y-pointer idiom)
     + sub r28,rX                    DYNAMIC frame (alloca/VLA) -> flagged, not bounded
   call / rcall                      +PC bytes, then the callee's subtree
   jmp / rjmp to another entry       tail call: subtree, but NO return address

-mcall-prologues (the PSI enables it to claw back 1,296 B of flash) is the detail a
stock avstack.pl gets wrong. Instead of open-coding push runs, functions TAIL-JUMP into
libgcc's __prologue_saves__ at a computed offset:
        ldi r26,lo8(frame) ; ldi r27,hi8(frame)
        ldi r30,.. ; ldi r31,..            <- Z = body
        jmp __prologue_saves__+K
__prologue_saves__ pushes r2..r17,r28,r29 (18 regs, one per 2 bytes of code), so entering
at +K executes (0x24-K)/2 of them, then subtracts X=r27:r26 from SP for the frame, then
ijmp's to the body. It is reached by JMP, not CALL, so it costs NO return address. A naive
push-counter sees an empty prologue and under-reports these functions by up to 18+frame
bytes EACH.

RETURN-ADDRESS SIZE is per-part and it matters:
   ATmega32U4   32 KB flash -> 16-bit PC -> CALL/RCALL push 2 B, IRQ entry pushes 2 B.
   ATmega2560  256 KB flash -> 17-bit PC -> CALL/RCALL push 3 B, IRQ entry pushes 3 B.

INTERRUPTS: AVR clears the global interrupt enable on ISR entry, so ISRs do not nest
unless something re-enables them. The tool searches each ISR's ENTIRE REACHABLE SUBTREE
(not just its own body) for a `sei`, and refuses the single-ISR assumption if it finds
one. That distinction is not academic: on the PSI the `sei` is four calls deep inside
FastLED's ClocklessController::showPixels, which the I2C ISR reaches. Where nesting is
impossible, total = deepest-sync-path + worst-single-ISR.

-----------------------------------------------------------------------------------
KNOWN LIMITS (read these before trusting the number)
-----------------------------------------------------------------------------------
* INDIRECT CALLS (icall). A static call graph cannot resolve a function pointer. We
  bound them by the worst subtree over the ADDRESS-TAKEN set: every function whose word
  address appears as a 16-bit LE word anywhere in the image (vtables, jump tables, ctor
  arrays) or is materialised by an ldi pair. That is a sound over-approximation of what
  an icall can reach, not a precise answer. Reported separately so you can see its size.
* JUMP TABLES (__tablejump2__). Switch cases are reached through a table the walk cannot
  follow. Compensated by ALSO charging every call site inside a function's address range
  -- even ones the CFG walk never reached -- at that function's maximum SP delta. Sound,
  slightly pessimistic.
* Charging unreached call sites at max-SP means a call in a shallow branch is billed at
  the function's deepest frame. Conservative by construction.
* This bounds the STACK. It does not bound the HEAP. If anything calls malloc/new, the
  heap grows up toward the stack and this tool will not see it.
* A bound is not a test. It says the stack cannot exceed N. It says nothing about whether
  the firmware is correct.

ESP32 / Xtensa is a DIFFERENT PROBLEM and is handled separately (--arch is sniffed from
the ELF). There, stack is per-FreeRTOS-task, the Arduino loop() runs in loopTask with an
8,192 B stack, and an overflow is DETECTED (canary/watchpoint -> panic + backtrace)
rather than silently corrupting globals as on AVR. Frames come from the windowed-ABI
`entry a1,N` instruction. A whole-task bound there is NOT achievable: the upstream
ESP-IDF/WiFi/newlib code reachable from loop() contains thousands of indirect calls and
genuine recursion (ArduinoJson's recursive-descent parser, std::__introsort_loop). The
tool therefore reports the deepest DIRECT-CALL path and, more usefully, the depth of the
DroidNet contract layer on its own -- the part we are responsible for.

Usage:
    ./test/host/stack_report.py                       # analyse .pio/build/*/firmware.elf
    ./test/host/stack_report.py --elf f.elf --mcu atmega2560
    ./test/host/stack_report.py --roots _Z12contractTickv     # ESP32: our layer only
    ./test/host/stack_report.py --json
Exit: 0 if the bound fits AND is sound; 1 if it does not fit, or recursion / ISR nesting
      / a dynamic frame makes the bound untrustworthy.

Copyright (c) 2026 Travis Cook. SPDX-License-Identifier: MIT
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

PARTS = {
    "atmega32u4": {"ram": 2560, "pc": 2},
    "atmega2560": {"ram": 8192, "pc": 3},
}

# Reached by JMP as part of a prologue/epilogue or a switch dispatch. NOT call edges.
PSEUDO = {"__prologue_saves__", "__epilogue_restores__", "__tablejump2__", "__tablejump__"}
# The hardware vector table and its default handler: dispatch, not calls.
NOT_A_FUNC = {"__vectors", "__bad_interrupt", "__stop_program", "__LOCK_REGION_LENGTH__"}

PUSH_LIST_LEN = 0x24          # offset of the first non-push insn in __prologue_saves__
PROLOGUE_REGS = 18            # r2..r17, r28, r29

RE_FUNC = re.compile(r"^([0-9a-fA-F]+)\s+<(.+)>:\s*$")
RE_INSN = re.compile(r"^\s*([0-9a-fA-F]+):\s+((?:[0-9a-fA-F]{2} )+)\s*\t(\S+)\s*(.*)$")
RE_SYM = re.compile(r"<([^>+]+)(?:\+0x([0-9a-fA-F]+))?>")
# Resolved branch/call target. objdump prints ABSOLUTE ops as `call 0x4a1c ; 0x4a1c <f>`
# but RELATIVE ones as a DISPLACEMENT: `rcall .-2138 ; 0x421e <f>`. Matching only the
# leading operand therefore drops every rcall/rjmp/brXX in the image (here: 198 rcall,
# 835 rjmp) and silently under-reports the whole program. Always take the address the
# disassembler resolved for us, just before the <symbol>.
RE_TGT = re.compile(r"0x([0-9a-fA-F]+)\s*<")
RE_DISP = re.compile(r"^\.([+-]\d+)")

# Conditional branches: fall through OR take the branch.
COND = {"brne", "breq", "brcs", "brcc", "brsh", "brlo", "brmi", "brpl", "brge", "brlt",
        "brhs", "brhc", "brts", "brtc", "brvs", "brvc", "brie", "brid", "brbs", "brbc"}
# Skip-next-instruction ops.
SKIP = {"cpse", "sbrc", "sbrs", "sbic", "sbis"}
STOP = {"ret", "reti"}


class Fn:
    __slots__ = ("addr", "name", "end", "sites", "max_sp", "icalls", "dynamic",
                 "has_sei", "notes", "unreached_calls")

    def __init__(self, addr, name):
        self.addr, self.name = addr, name
        self.end = addr
        self.sites = []          # (sp, kind, target)  kind: call|tail|icall
        self.max_sp = 0
        self.icalls = 0
        self.dynamic = False
        self.has_sei = False
        self.notes = []
        self.unreached_calls = []


def sh(*a):
    return subprocess.run(a, capture_output=True, text=True, check=True).stdout


def load(objdump, readelf, elf):
    """Parse the ELF into: insns[addr] = (mnemonic, ops, size), and the symbol map."""
    insns = {}
    order = []
    for line in sh(objdump, "-d", elf).splitlines():
        m = RE_INSN.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        nbytes = len(m.group(2).split())
        insns[addr] = (m.group(3), m.group(4).strip(), nbytes)
        order.append(addr)

    # Symbols: FUNC gives us real entries with sizes; NOTYPE .text labels (libgcc asm,
    # ISR vectors) are entries too but carry no size.
    syms = {}     # addr -> name (preferring FUNC)
    funcsz = {}
    for line in sh(readelf, "-sW", elf).splitlines():
        p = line.split()
        if len(p) < 8 or ":" not in p[0]:
            continue
        try:
            val = int(p[1], 16)
            size = int(p[2])
        except ValueError:
            continue
        typ, name = p[3], p[7]
        if typ == "FUNC":
            syms[val] = name
            funcsz[val] = size
        elif typ == "NOTYPE" and name not in NOT_A_FUNC:
            syms.setdefault(val, name)
    return insns, order, syms, funcsz


def entry_set(insns, syms, funcsz):
    """
    Function entries = FUNC symbols + ISR vectors + anything actually CALLED.

    Deliberately NOT jmp/rjmp targets. A jmp lands on a function entry only for a tail
    call; far more often it is an intra-routine branch or a shared continuation label
    inside hand-written libgcc asm (Ldone_5678, __fp_round, __divsf3_pse ...). Promoting
    those to "functions" invents recursion that does not exist (they rjmp back and forth)
    and drags linker/startup labels (__dtors_end, .do_clear_bss_start) into the call
    graph. Leaving them out means the address-level walk simply CONTINUES through them,
    carrying the SP delta -- which is both correct and what we want. Genuine tail calls
    still work, because their target is a FUNC symbol and therefore already an entry.
    """
    entries = {a: syms[a] for a in funcsz if a in syms}
    for addr, (mn, ops, _) in insns.items():
        if mn not in ("call", "rcall"):
            continue
        sym = RE_SYM.search(ops)
        t = target(insns, addr, ops)
        if t is None or not sym or sym.group(2):
            continue
        if sym.group(1) in NOT_A_FUNC:
            continue
        if t in syms:
            entries.setdefault(t, syms[t])
    # Real ISRs only. Every UNUSED vector is a WEAK NOTYPE alias of __bad_interrupt,
    # and __bad_interrupt does `rjmp __vectors` -> address 0 -> the RESET path -> main.
    # Walking those aliases follows a soft reset into the entire program and reports a
    # nonsense ~370 B "ISR". A real ISR is a FUNC symbol with a nonzero size.
    for a, n in syms.items():
        if re.match(r"^__vector_\d+$", n) and funcsz.get(a, 0) > 0:
            entries[a] = n
    return entries


def frame_from(insns, addr, order_idx):
    """Y-pointer frame idiom starting at `in r28,0x3d`. Returns (bytes, dynamic?)."""
    # look ahead a few instructions for the SP adjustment
    a = addr
    for _ in range(6):
        a = next_addr(insns, a)
        if a is None or a not in insns:
            break
        mn, ops, _ = insns[a]
        if mn == "sbiw" and ops.startswith("r28"):
            m = re.search(r"0x([0-9a-fA-F]+)", ops)
            return (int(m.group(1), 16) if m else 0), False
        if mn == "subi" and ops.startswith("r28"):
            m = re.search(r"0x([0-9a-fA-F]+)", ops)
            lo = int(m.group(1), 16) if m else 0
            hi = 0
            b = next_addr(insns, a)
            if b in insns and insns[b][0] == "sbci" and insns[b][1].startswith("r29"):
                m2 = re.search(r"0x([0-9a-fA-F]+)", insns[b][1])
                hi = int(m2.group(1), 16) if m2 else 0
            val = (hi << 8) | lo
            if 0 < val < 0x8000:
                return val, False
            return 0, False
        if mn in ("sub",) and ops.startswith("r28"):
            return 0, True          # SP adjusted by a register -> alloca / VLA
    return 0, False


def next_addr(insns, a):
    if a not in insns:
        return None
    return a + insns[a][2]


def target(insns, a, ops):
    """Resolved target of a branch/call at `a`, from the disassembler's comment."""
    m = RE_TGT.search(ops)
    if m:
        return int(m.group(1), 16)
    m = RE_DISP.match(ops)          # relative with no symbol: PC_next + disp
    if m:
        nx = next_addr(insns, a)
        return (nx + int(m.group(1))) if nx is not None else None
    return None


def walk(entry, name, insns, entries, pc, size=0):
    """
    Address-level CFG walk from one entry. Returns an Fn with max SP delta and the
    call sites annotated with the SP depth at which they occur.

    `size` is the ELF st_size of a FUNC symbol (0 for the unsized NOTYPE asm labels in
    libgcc). When we have it, the function body is a HARD BOUNDARY: gcc never branches
    out of a function except by call or by tail-jump to another entry, so any other
    target outside [entry, entry+size) means we mis-decoded something -- stop rather
    than wander off into unrelated code and invent edges that do not exist.
    """
    fn = Fn(entry, name)
    lo, hi = entry, (entry + size if size else None)
    seen = {}                       # addr -> max sp seen there
    work = [(entry, 0, {})]         # (addr, sp, regfile of ldi immediates)
    growth = False

    while work:
        a, sp, regs = work.pop()
        if a is None or a not in insns:
            continue
        # Address 0 is the RESET vector. A `call 0` / `jmp 0` is a null function-pointer
        # call (Flthy's serialEventRun has several: undefined weak serialEvent1/2/3
        # handlers, guarded by an always-taken breq, so they are dead code -- but a CFG
        # walk explores both arms of a branch). Following it walks the reset path into
        # __ctors_end -> startup -> `call main`, which fabricates a main->main cycle and
        # an infinite bound. A reset re-initialises SP; it costs no stack. Terminate.
        if a == 0:
            fn.notes.append("null/reset target (call 0) -- dead code, not followed")
            continue
        if hi is not None and not (lo <= a < hi):
            fn.notes.append("left function body @0x%x" % a)
            continue
        if a in seen:
            if sp <= seen[a]:
                continue
            if sp - seen[a] > 256:
                growth = True       # SP climbing across a back edge -> unbounded loop
                continue
        seen[a] = max(sp, seen.get(a, 0))
        fn.max_sp = max(fn.max_sp, sp)
        fn.end = max(fn.end, a)

        mn, ops, _ = insns[a]
        nxt = next_addr(insns, a)

        if mn == "push":
            work.append((nxt, sp + 1, regs))
            continue
        if mn == "pop":
            work.append((nxt, max(0, sp - 1), regs))
            continue
        if mn == "sei":
            fn.has_sei = True
        if mn == "ldi":
            m = re.match(r"r(\d+),\s*0x([0-9a-fA-F]+)", ops)
            if m:
                regs = dict(regs)
                regs[int(m.group(1))] = int(m.group(2), 16)
            work.append((nxt, sp, regs))
            continue
        if mn == "in" and ops.startswith("r28, 0x3d"):
            f, dyn = frame_from(insns, a, None)
            if dyn:
                fn.dynamic = True
            work.append((nxt, sp + f, regs))
            continue
        if mn in STOP:
            continue
        if mn in ("icall", "eicall"):
            fn.icalls += 1
            fn.sites.append((sp, "icall", None))
            work.append((nxt, sp, regs))
            continue
        if mn == "ijmp":
            # Only reachable here inside __prologue_saves__/__tablejump2__, which we
            # never walk as ordinary functions. Treat as a terminator + flag.
            fn.notes.append("ijmp @0x%x" % a)
            continue

        if mn in ("call", "rcall", "jmp", "rjmp"):
            sym = RE_SYM.search(ops)
            t = target(insns, a, ops)
            nm = sym.group(1) if sym else None
            off = sym.group(2) if sym else None

            if nm == "__prologue_saves__":
                k = int(off, 16) if off else 0
                npush = max(0, (PUSH_LIST_LEN - k) // 2)
                frame = (regs.get(27, 0) << 8) | regs.get(26, 0)
                fn.notes.append("call-prologue +0x%x: %d regs + %d B frame"
                                % (k, npush, frame))
                # __prologue_saves__ ijmp's to Z = body, which GCC always places at the
                # very next instruction. Verify against Z when we have it.
                body = ((regs.get(31, 0) << 8) | regs.get(30, 0)) * 2
                work.append((body if body in insns else nxt, sp + npush + frame, regs))
                continue
            if nm in ("__epilogue_restores__",):
                continue            # returns
            if nm == "__tablejump2__" or nm == "__tablejump__":
                # switch dispatch; cases handled by the unreached-call-site sweep
                fn.notes.append("jump table")
                continue

            if mn in ("call", "rcall"):
                if t in entries:
                    fn.sites.append((sp, "call", entries[t]))
                work.append((nxt, sp, regs))
                continue

            # jmp / rjmp
            if t is not None and t in entries and t != entry and not off:
                fn.sites.append((sp, "tail", entries[t]))
                continue            # tail call terminates this path
            if t is not None:
                work.append((t, sp, regs))   # intra-function branch (or self-loop)
            continue

        if mn in COND:
            t = target(insns, a, ops)
            if t is not None:
                work.append((t, sp, regs))
            work.append((nxt, sp, regs))
            continue
        if mn in SKIP:
            work.append((nxt, sp, regs))
            n2 = next_addr(insns, nxt) if nxt else None
            work.append((n2, sp, regs))
            continue

        work.append((nxt, sp, regs))

    if growth:
        fn.notes.append("SP GROWS ACROSS A BACK EDGE -- possible unbounded loop")
    return fn, seen



# =============================== XTENSA / ESP32 =====================================
# Windowed ABI: `entry a1, N` allocates an N-byte frame (N already includes the 16-byte
# register-window save area, which is where window-overflow spills land). There is no
# per-call return-address push to add -- the return address lives in a0 and is spilled
# into the caller's own frame. So the depth of a path is simply the sum of its `entry`
# frames. CALL0-ABI code (hand-written asm, some IRAM handlers) instead does
# `addi a1, a1, -N`; we take that too.
RE_XF = re.compile(r"^([0-9a-f]{8}) <(.+)>:$")
RE_XI = re.compile(r"^([0-9a-f]{8}):\s+[0-9a-f ]+\s+(\S+)\s*(.*)$")
XCALL = ("call0", "call4", "call8", "call12")

ARDUINO_LOOP_STACK = 8192          # cores/esp32/main.cpp: ARDUINO_LOOP_STACK_SIZE
# The DroidNet contract layer's own entry points -- the code this fork is responsible
# for. Everything else reachable from loop() is upstream (WiFi, newlib, Reeltwo).
CONTRACT_ROOTS = [
    "_Z12contractTickv",
    "_Z13contractParsePKcR14ParsedContract",
    "_Z19applyContractToUnithRK14ParsedContract",
    "_Z14ContractRenderR19LogicEngineRenderer",
    "_ZL18Animation_ContractR15AnimationPlayerjmm",
]
TASK_ROOTS = ["_Z8loopTaskPv", "_Z5setupv", "_Z4loopv"]


def xtensa_report(objdump, elf, args):
    import collections
    dis = sh(objdump, "-d", elf).splitlines()
    funcs, frame = {}, {}
    calls = collections.defaultdict(list)
    icalls = collections.Counter()
    cur = None
    for line in dis:
        m = RE_XF.match(line)
        if m:
            cur = m.group(2)
            funcs[cur] = int(m.group(1), 16)
            frame.setdefault(cur, 0)
            continue
        if cur is None:
            continue
        m = RE_XI.match(line)
        if not m:
            continue
        mn, ops = m.group(2), m.group(3)
        if mn == "entry":
            mm = re.search(r"a1,\s*(0x[0-9a-f]+|\d+)", ops)
            if mm:
                frame[cur] = max(frame[cur], int(mm.group(1), 0))
        elif mn == "addi" and re.match(r"a1,\s*a1,\s*-\d+", ops):
            frame[cur] = max(frame[cur], int(re.search(r"-(\d+)", ops).group(1)))
        elif mn in XCALL:
            mm = re.search(r"<([^>+]+)>", ops)
            if mm:
                calls[cur].append(mm.group(1))
        elif mn.startswith("callx"):
            icalls[cur] += 1

    sys.setrecursionlimit(100000)
    memo, onstk, cyc = {}, set(), set()

    def cost(f):
        if f in memo:
            return memo[f]
        if f in onstk:
            cyc.add(f)
            return (0, [f + " [RECURSION]"])
        if f not in funcs:
            return (0, [])
        onstk.add(f)
        best, bp = 0, []
        for c in calls.get(f, []):
            sub, pth = cost(c)
            if sub > best:
                best, bp = sub, pth
        onstk.discard(f)
        memo[f] = (frame[f] + best, [f] + bp)
        return memo[f]

    def subgraph(f, seen):
        if f in seen or f not in funcs:
            return
        seen.add(f)
        for c in calls.get(f, []):
            subgraph(c, seen)

    W = 82
    print("=" * W)
    print("  STACK -- ESP32 / Xtensa (windowed ABI)")
    print("  %s" % elf)
    print("=" * W)
    print("  Arduino loopTask stack   %6d B   (ARDUINO_LOOP_STACK_SIZE)"
          % ARDUINO_LOOP_STACK)
    print("  ESP32 overflow is DETECTED (FreeRTOS canary / watchpoint -> panic +")
    print("  backtrace). It does NOT silently corrupt globals the way AVR does.")
    print("")
    print("  DROIDNET CONTRACT LAYER -- the code this fork owns")
    print("  " + "-" * (W - 4))
    worst = 0
    for t in CONTRACT_ROOTS:
        if t not in funcs:
            continue
        memo.clear(); onstk.clear(); cyc.clear()
        c, path = cost(t)
        seen = set(); subgraph(t, seen)
        ic = sum(icalls[f] for f in seen)
        worst = max(worst, c)
        print("    %-46s %5d B  (%2d fns, %2d callx%s)"
              % (t[:46], c, len(seen), ic, ", RECURSION" if cyc else ""))
    print("  " + "-" * (W - 4))
    print("    contract layer worst-case depth            %5d B  = %.1f%% of loopTask"
          % (worst, 100.0 * worst / ARDUINO_LOOP_STACK))
    print("")
    print("  WHOLE loopTask (direct calls only -- see caveat below)")
    print("  " + "-" * (W - 4))
    for t in TASK_ROOTS:
        if t not in funcs:
            continue
        memo.clear(); onstk.clear(); cyc.clear()
        c, path = cost(t)
        print("    %-30s %5d B" % (t, c))
        if args.max_depth and t == "_Z8loopTaskPv":
            run = 0
            for fn in path[:args.max_depth]:
                run += frame.get(fn, 0)
                print("        %-50s %5d B (cum %5d)%s"
                      % (fn[:50], frame.get(fn, 0), run,
                         "  [%d callx]" % icalls[fn] if icalls[fn] else ""))
    seen = set()
    for t in TASK_ROOTS:
        subgraph(t, seen)
    memo.clear(); onstk.clear(); cyc.clear()
    for t in TASK_ROOTS:
        cost(t)
    tot_ic = sum(icalls[f] for f in seen)
    print("")
    print("  SOUNDNESS -- the whole-task figure is NOT a bound")
    print("    %d functions reachable from loop()/setup(); %d INDIRECT (callx) sites"
          % (len(seen), tot_ic))
    print("    among them, and genuine RECURSION in upstream code (ArduinoJson's")
    print("    recursive-descent parser, std::__introsort_loop, lwIP netconn_*).")
    print("    A sound static bound for the whole task is therefore NOT achievable.")
    print("    What IS bounded: the DroidNet contract layer above (%d B, no recursion)."
          % worst)
    print("    The deep upstream path is WiFi init -> printf -> _svfprintf_r (784 B) ->")
    print("    _dtoa_r -- newlib float formatting, not our code.")
    return 0


def main():
    ap = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                 description=__doc__)
    ap.add_argument("--elf")
    ap.add_argument("--mcu", choices=sorted(PARTS))
    ap.add_argument("--objdump")
    ap.add_argument("--static", type=int)
    ap.add_argument("--roots", default="main")
    ap.add_argument("--max-depth", type=int, default=40)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    elf = args.elf
    if not elf:
        for d, _, fs in os.walk(os.path.join(root_dir, ".pio", "build")):
            for f in fs:
                if f == "firmware.elf":
                    elf = os.path.join(d, f)
        if not elf:
            sys.exit("no firmware.elf -- run `pio run` first, or pass --elf")

    # Sniff the architecture from the ELF machine type and dispatch.
    xt_od = os.path.expanduser(
        "~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-objdump")
    try:
        hdr = sh("file", elf)
    except Exception:
        hdr = ""
    is_xtensa = "Tensilica" in hdr or "Xtensa" in hdr
    if not is_xtensa and os.path.exists(xt_od):
        try:
            if "Tensilica" in sh(xt_od, "-f", elf):
                is_xtensa = True
        except subprocess.CalledProcessError:
            pass
    if is_xtensa:
        if not os.path.exists(xt_od):
            sys.exit("xtensa objdump not found")
        return xtensa_report(xt_od, elf, args)

    tc = os.path.expanduser("~/.platformio/packages/toolchain-atmelavr/bin")
    objdump = args.objdump or os.path.join(tc, "avr-objdump")
    readelf = os.path.join(tc, "avr-readelf")
    size = os.path.join(tc, "avr-size")
    for t in (objdump, readelf):
        if not os.path.exists(t):
            alt = shutil.which(os.path.basename(t))
            if not alt:
                sys.exit("missing %s" % t)

    mcu = args.mcu
    if not mcu:
        ini = os.path.join(root_dir, "platformio.ini")
        txt = open(ini).read().lower() if os.path.exists(ini) else ""
        mcu = "atmega32u4" if ("promicro" in txt or "32u4" in txt) else \
              ("atmega2560" if ("mega" in txt or "2560" in txt) else None)
        if not mcu:
            sys.exit("cannot sniff MCU -- pass --mcu")
    part = PARTS[mcu]
    pc = part["pc"]

    insns, order, syms, funcsz = load(objdump, readelf, elf)
    entries = entry_set(insns, syms, funcsz)

    fns = {}
    for a, n in entries.items():
        f, seen = walk(a, n, insns, entries, pc, funcsz.get(a, 0))
        # Sweep call sites the CFG walk never reached (switch cases behind a jump
        # table). Charge them at the function's deepest SP -- sound, a bit pessimistic.
        end = a + funcsz.get(a, 0)
        if end > a:
            for ia in range(a, end):
                if ia in insns and ia not in seen:
                    mn, ops, _ = insns[ia]
                    if mn in ("call", "rcall"):
                        t = target(insns, ia, ops)
                        if t in entries:
                            f.sites.append((f.max_sp, "call", entries[t]))
                            f.unreached_calls.append(entries[t])
                    elif mn in ("icall", "eicall"):
                        f.icalls += 1
                        f.sites.append((f.max_sp, "icall", None))
        fns[n] = f

    # ---- address-taken set: sound superset of what an icall can reach ---------------
    # Two sources, and ONLY these two -- deliberately NOT a raw scan of .text, which
    # produces garbage (instruction words coincidentally equal function word-addresses,
    # which pulled 115 bogus "targets" and a nonsense deepest path out of an earlier
    # version of this script).
    #   (a) function pointers sitting in initialised data: C++ VTABLES live in .data on
    #       AVR, and so does any static table of function pointers. Plus the .ctors
    #       array, which lives in .text between __ctors_start/__ctors_end.
    #   (b) function pointers MATERIALISED BY CODE: `ldi rN,lo8(gs(f)) / ldi rN+1,hi8(..)`
    #       is how &f reaches a callback registration such as Wire.onReceive(f).
    wordaddr = {a >> 1: n for a, n in entries.items() if a % 2 == 0}
    taken = set()

    def scan_words(section, lo=None, hi=None):
        try:
            out = sh(objdump, "-s", "-j", section, elf)
        except subprocess.CalledProcessError:
            return
        mem = {}
        for line in out.splitlines():
            m = re.match(r"^\s*([0-9a-fA-F]+)\s((?:[0-9a-fA-F]{2,8}\s)+)", line)
            if not m:
                continue
            base = int(m.group(1), 16)
            blob = "".join(m.group(2).split())
            for i in range(0, len(blob), 2):
                mem[base + i // 2] = int(blob[i:i + 2], 16)
        for a in sorted(mem):
            if lo is not None and not (lo <= a < hi):
                continue
            if a + 1 not in mem:
                continue
            w = (mem[a + 1] << 8) | mem[a]
            if w in wordaddr:
                taken.add(wordaddr[w])

    scan_words(".data")
    ctors = [a for a, n in syms.items() if n == "__ctors_start"]
    ctore = [a for a, n in syms.items() if n == "__ctors_end"]
    if ctors and ctore:
        scan_words(".text", ctors[0], ctore[0])

    # (b) ldi-pair materialisation of a function address, in either register order.
    ldis = {}
    for a in order:
        mn, ops, _ = insns[a]
        if mn == "ldi":
            m = re.match(r"r(\d+),\s*0x([0-9a-fA-F]+)", ops)
            if m:
                ldis[a] = (int(m.group(1)), int(m.group(2), 16))
    addrs = sorted(ldis)
    for i, a in enumerate(addrs):
        rn, va = ldis[a]
        for b in addrs[i + 1:i + 3]:          # allow a 1-instruction gap
            rm, vb = ldis[b]
            w = None
            if rm == rn + 1:
                w = (vb << 8) | va            # lo then hi
            elif rm == rn - 1:
                w = (va << 8) | vb            # hi then lo
            if w is not None and w in wordaddr:
                taken.add(wordaddr[w])

    # ---- memoised subtree cost with real cycle detection ----------------------------
    memo, oncall, cycles = {}, set(), set()

    def cost(name, ind):
        """(bytes, path) where path = [(fn, own_frame, how_we_got_here), ...]."""
        if name in memo:
            return memo[name]
        if name in oncall:
            cycles.add(name)
            return (0, [(name + " [RECURSION]", 0, "call")])
        f = fns.get(name)
        if f is None:
            return (0, [])
        oncall.add(name)
        best, bpath, bkind = f.max_sp, [], None
        for sp, kind, tgt in f.sites:
            if kind == "call":
                sub, pth = cost(tgt, ind)
                c = sp + pc + sub
            elif kind == "tail":
                sub, pth = cost(tgt, ind)
                c = sp + sub
            else:
                if ind is None:
                    continue
                sub, pth = ind
                c = sp + pc + sub
            if c > best:
                best, bpath, bkind = c, pth, kind
        oncall.discard(name)
        if bpath and bkind:
            head = list(bpath[0])
            head[2] = bkind
            bpath = [tuple(head)] + list(bpath[1:])
        memo[name] = (best, [(name, f.max_sp, "call")] + bpath)
        return memo[name]

    isr_names = sorted(n for n, f in fns.items()
                       if re.match(r"^__vector_\d+$", n) and funcsz.get(f.addr, 0) > 0)
    # An icall target must be a real function. Startup/linker labels (__dtors_end,
    # .do_clear_bss_start, __ctors_*) are not callable through a function pointer, and
    # letting them in lets the bound "reach" main through the reset path -- nonsense.
    real = {syms[a] for a in funcsz if a in syms}
    skip = set(isr_names) | PSEUDO | NOT_A_FUNC | {"main", "__do_global_ctors"}
    taken = {t for t in taken if t in real}

    # pass 1: no indirect bound, to size the address-taken candidates
    ind_best, ind_who, ind_path = 0, None, []
    for n in sorted(taken):
        if n in skip:
            continue
        memo.clear()
        c, p = cost(n, None)
        if c > ind_best:
            ind_best, ind_who, ind_path = c, n, p
    ind = (ind_best, ind_path)

    # pass 2a: DIRECT calls only (icall edges ignored) -- shows how much of the final
    # number is the indirect over-approximation rather than real, provable call depth.
    memo.clear()
    cycles.clear()
    root0 = args.roots.split(",")[0].strip()
    sync_direct, _ = cost(root0, None)
    isr_direct = max([cost(n, None)[0] + pc for n in isr_names] or [0])

    # pass 2b: the SOUND bound, with every icall charged at the address-taken worst case
    memo.clear()
    cycles.clear()
    sync, sync_path = cost(root0, ind)
    isrs = {}
    for n in isr_names:
        c, p = cost(n, ind)
        isrs[n] = (c + pc, p)      # + hardware PC push on IRQ entry
    worst_isr, worst_isr_path = max(isrs.values(), key=lambda r: r[0]) if isrs else (0, [])
    # ISR NESTING -- this check MUST be TRANSITIVE. Looking only at an ISR's own body
    # is not enough and gives a dangerously wrong "ok": on the PSI the `sei` is not in
    # any ISR, it is four calls deep inside FastLED's ClocklessController::showPixels
    # (cli -> bit-bang WS2812 -> patch timer0_millis -> UNCONDITIONAL sei), which the
    # TWI/I2C ISR reaches via receiveEvent -> parseContract -> allOFF -> FastLED.show().
    # An ISR that re-enables interrupts can be interrupted -- including BY ITSELF.
    def sei_in_subtree(root):
        """(reaches_sei, culprit) over everything the ISR can call, icalls included."""
        seen, stack = set(), [root]
        while stack:
            x = stack.pop()
            if x in seen or x not in fns:
                continue
            seen.add(x)
            f = fns[x]
            if f.has_sei:
                return True, x
            for _sp, kind, tgt in f.sites:
                if tgt:
                    stack.append(tgt)
                elif kind == "icall":
                    stack.extend(taken)      # an icall may land on any address-taken fn
        return False, None

    nesting = []
    nest_why = {}
    for n in isr_names:
        hit, who = sei_in_subtree(n)
        if hit:
            nesting.append(n)
            nest_why[n] = who
    dynamic = sorted(n for n, f in fns.items() if f.dynamic and n not in PSEUDO)

    static = args.static
    if static is None:
        static = 0
        try:
            for line in sh(size, "-A", elf).splitlines():
                p = line.split()
                if len(p) >= 2 and p[0] in (".data", ".bss"):
                    static += int(p[1])
        except Exception:
            pass

    total = sync + worst_isr
    avail = part["ram"] - static
    head = avail - total
    sound = not cycles and not nesting and not dynamic

    if args.json:
        print(json.dumps({
            "mcu": mcu, "elf": elf, "ram": part["ram"], "static": static,
            "available": avail, "sync": sync, "worst_isr": worst_isr, "total": total,
            "headroom": head, "sound": sound, "recursion": sorted(cycles),
            "isr_nesting": nesting, "dynamic_frames": dynamic,
            "indirect_sites": sum(f.icalls for f in fns.values()),
            "indirect_bound": ind_best, "indirect_worst": ind_who,
            "address_taken": sorted(taken),
            "isrs": {k: v[0] for k, v in isrs.items()},
            "path": [{"fn": a, "own": b} for a, b in sync_path],
        }, indent=2))
        return 0 if (head > 0 and sound) else 1

    W = 82
    p = print
    p("=" * W)
    p("  WORST-CASE STACK BOUND -- %s" % mcu)
    p("  %s" % elf)
    p("=" * W)
    p("  SRAM total            %6d B" % part["ram"])
    p("  static (.data+.bss)   %6d B" % static)
    p("  available for stack   %6d B" % avail)
    p("")
    p("  DEEPEST SYNCHRONOUS PATH   (root=%s, return addr=%d B/call)"
      % (args.roots, pc))
    p("  " + "-" * (W - 4))
    run = 0
    for i, ent in enumerate(sync_path[:args.max_depth]):
        nm, own, how = ent
        run += own + (pc if i and how != "tail" else 0)
        f = fns.get(nm)
        tag = ""
        if f:
            b = []
            if f.dynamic:
                b.append("DYNAMIC FRAME")
            tag = ("   [" + ", ".join(b) + "]") if b else ""
        arrow = {"call": "", "tail": " (tail)", "icall": " <-- via ICALL (bound, not a"
                                                          " proven edge)"}.get(how, "")
        p("  %2d. %-44s %4d B  (cum %4d)%s%s" % (i, nm[:44], own, run, arrow, tag))
    if len(sync_path) > args.max_depth:
        p("      ... %d more frames" % (len(sync_path) - args.max_depth))
    p("  " + "-" * (W - 4))
    p("  synchronous subtotal                %6d B" % sync)
    p("")
    p("  INTERRUPTS  (hardware pushes %d B on entry; see SOUNDNESS for nesting)" % pc)
    for n, (c, pp) in sorted(isrs.items(), key=lambda kv: -kv[1][0]):
        chain = " -> ".join(x[0] for x in pp[1:3])
        p("    %-24s %4d B   %s" % (n, c, chain))
    p("    %-24s %4d B   <-- charged ONCE" % ("WORST ISR", worst_isr))
    p("")
    p("=" * W)
    p("  direct calls only     %6d B  = %d (sync) + %d (worst ISR)   <- proven edges"
      % (sync_direct + isr_direct, sync_direct, isr_direct))
    p("  TOTAL WORST CASE      %6d B  = %d (sync) + %d (worst ISR)   <- SOUND BOUND"
      % (total, sync, worst_isr))
    p("  AVAILABLE             %6d B" % avail)
    p("  HEADROOM              %6d B  (%.0f%% of the stack budget consumed)"
      % (head, 100.0 * total / avail if avail else 0))
    p("=" * W)
    p("")
    p("  SOUNDNESS")
    if cycles:
        p("    FAIL  RECURSION -- the bound is INFINITE, not %d: %s"
          % (total, ", ".join(sorted(cycles))))
    else:
        p("    ok    no recursion anywhere in the call graph (bound is finite)")
    if nesting:
        p("    FAIL  ISRs CAN NEST. These ISRs reach an unconditional `sei`:")
        for n in nesting:
            p("            %-16s -> sei inside %s" % (n, nest_why[n][:44]))
        p("          Interrupts get re-enabled WHILE STILL INSIDE the ISR, so another")
        p("          interrupt -- INCLUDING THE SAME ONE -- can stack on top. The")
        p("          'sync + one ISR' model above is therefore NOT a bound, and a")
        p("          self-re-entering ISR makes the true worst case UNBOUNDED.")
        p("          Worst case with ONE level of nesting: %d B (%d B headroom)."
          % (total + worst_isr, avail - total - worst_isr))
    else:
        p("    ok    no ISR reaches a `sei` (transitively) -> ISRs cannot nest")
    if dynamic:
        p("    FAIL  dynamic frame (alloca/VLA), size not statically known: %s"
          % ", ".join(dynamic))
    else:
        p("    ok    no alloca/VLA -- every frame is a compile-time constant")
    ni = sum(f.icalls for f in fns.values())
    p("    ~     %d icall sites, bounded by the worst of %d address-taken functions"
      % (ni, len(taken)))
    p("          (%s = %d B). Sound over-approximation, not a precise answer."
      % (ind_who, ind_best))
    p("")
    p("  VERDICT: %s" % ("FITS -- %d B spare" % head if head > 0 and sound
                         else ("OVERFLOWS by %d B" % -head if head <= 0
                               else "FITS but the bound is NOT SOUND (see above)")))
    return 0 if (head > 0 and sound) else 1


if __name__ == "__main__":
    sys.exit(main())
