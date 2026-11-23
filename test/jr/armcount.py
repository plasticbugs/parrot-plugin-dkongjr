#!/usr/bin/env python3
# armcount.py BENCH.elf ROMS.bin SCRIPT.txt FROM-TO [FROM-TO ...]
#
# How long the cart's M33 takes over each list: runs test/jr/build-arm.sh's
# build of src/jrplay.c in Unicorn and charges each instruction the
# Cortex-M33's time for it (from Arm's Cortex-M33 technical reference
# manual: most instructions 1 cycle, a load 2, a taken branch 3, ...), all
# in SRAM at 150 MHz. Lists outside the ranges run uncounted (fast).
# ROMS.bin is test/jr/jrlists --roms-out; SCRIPT.txt is jrhost's --script.
#
# Needs unicorn, capstone and pyelftools (pip, in a venv).
import sys, struct, re
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_BLOCK
from unicorn.arm_const import UC_CPU_ARM_MAX, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS

LIST_BUDGET = 2_516_000     # a list is 16.77 ms of the console's time: 2.52M cycles at 150 MHz

elf_path, roms_path, script_path = sys.argv[1:4]
profile = '--profile' in sys.argv
hot = sys.argv[sys.argv.index('--hot') + 1] if '--hot' in sys.argv else None   # a function's busiest blocks
args = [a for i, a in enumerate(sys.argv[4:], 4) if a not in ('--profile', '--hot') and sys.argv[i - 1] != '--hot']
ranges = [tuple(int(v) for v in r.split('-')) for r in args]

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
uc.ctl_set_cpu_model(UC_CPU_ARM_MAX)     # ARMv8's FP (vsel, vminnm), which the M33 has and an A15 doesn't
uc.mem_map(0x20000000, 0x80000)
sym = {}
with open(elf_path, 'rb') as f:
    elf = ELFFile(f)
    for seg in elf.iter_segments():
        if seg['p_type'] == 'PT_LOAD' and seg['p_filesz']:
            uc.mem_write(seg['p_paddr'], seg.data())
    funcs = []
    for s in elf.get_section_by_name('.symtab').iter_symbols():
        sym[s.name] = s['st_value']
        if s['st_info']['type'] == 'STT_FUNC':
            funcs.append((s['st_value'] & ~1, s['st_size'], s.name))
    code = {}
    for name in ('.text',):
        sec = elf.get_section_by_name(name)
        code_base, code_bytes = sec['sh_addr'], sec.data()
uc.mem_write(sym['__bss_start'], bytes(sym['__bss_end'] - sym['__bss_start']))
uc.mem_write(sym['bench_roms'], open(roms_path, 'rb').read())
ev = re.findall(r'(\d+):(in0|in2)=([0-9a-fA-F]+)', open(script_path).read())
blob = b''.join(struct.pack('<IBBxx', int(f), 0 if p == 'in0' else 2, int(v, 16)) for f, p, v in ev)
uc.mem_write(sym['bench_ev'], blob)
uc.mem_write(sym['bench_nev'], struct.pack('<I', len(ev)))
uc.reg_write(UC_ARM_REG_C1_C0_2, uc.reg_read(UC_ARM_REG_C1_C0_2) | (0xF << 20))   # the FPU on
uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)

# ---- the M33's times --------------------------------------------------------------------
cs = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
BRANCH = {'b', 'bl', 'blx', 'bx', 'cbz', 'cbnz', 'tbb', 'tbh'}

def cost(insn):
    m = insn.mnemonic.split('.')[0]
    base = re.sub(r'(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)$', '', m) if m not in BRANCH else m
    ops = insn.op_str
    if m.startswith('it'):
        return 0                # folded
    if base in ('b', 'cbz', 'cbnz'):
        return 1                # +2 when taken, charged at the next block
    if base in ('bl', 'blx', 'bx'):
        return 3
    if base in ('tbb', 'tbh'):
        return 4
    if base in ('push', 'pop', 'ldm', 'ldmia', 'ldmdb', 'stm', 'stmia', 'stmdb', 'vpush', 'vpop', 'vldmia', 'vstmia', 'vldmdb', 'vstmdb'):
        n = ops.count(',') + 1 if '{' in ops else 1
        n = len(re.findall(r'[rsdl][0-9]+|lr|pc|sb|sl|fp|ip', ops[ops.find('{'):]))
        return 1 + n + (2 if 'pc' in ops and base in ('pop', 'ldm', 'ldmia') else 0)
    if base in ('ldrd',):
        return 3
    if base in ('strd',):
        return 2
    if base.startswith('ldr') or base.startswith('ldrex'):
        return 2 + (2 if 'pc' in ops.split(',')[0] else 0)
    if base.startswith('str'):
        return 1
    if base in ('sdiv', 'udiv'):
        return 6
    if base in ('vdiv', 'vsqrt'):
        return 14
    if base in ('vmla', 'vmls', 'vnmla', 'vnmls', 'vfma', 'vfms', 'vfnma', 'vfnms'):
        return 3
    if base in ('vldr', 'vstr'):
        return 2
    if base == 'vmov' and ops.count(',') == 2:
        return 2                # two core registers
    return 1

blocks = {}
def block_cost(addr, size):
    k = (addr, size)
    c = blocks.get(k)
    if c is None:
        off = addr - code_base
        insns = list(cs.disasm(code_bytes[off:off + size], addr))
        t = sum(cost(i) for i in insns)
        last = insns[-1] if insns else None
        cond = last is not None and last.mnemonic.split('.')[0] in ('b', 'cbz', 'cbnz') or (
            last is not None and re.match(r'b(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)$', last.mnemonic.split('.')[0]))
        c = blocks[k] = (t, addr + size, bool(cond))
    return c

import bisect
funcs.sort()
fstart = [f[0] for f in funcs]
def func_of(addr):
    i = bisect.bisect_right(fstart, addr) - 1
    return funcs[i][2] if i >= 0 else '?'
per_func = {}
per_block = {}
state = {'cycles': 0, 'fall': None, 'cond': False}
def on_block(uc, addr, size, _):
    t, end, cond = block_cost(addr, size)
    if state['cond'] and addr != state['fall']:
        state['cycles'] += 2   # the branch before was taken
    state['cycles'] += t
    state['fall'], state['cond'] = end, cond
    if profile or hot:
        f = func_of(addr)
        per_func[f] = per_func.get(f, 0) + t
        if f == hot:
            per_block[(addr, size)] = per_block.get((addr, size), 0) + t

mark = sym['bench_mark'] & ~1
pc = sym['_start'] | 1
last = max(b for a, b in ranges)
hook = None
res = []
for n in range(1, last + 1):
    counted = any(a <= n <= b for a, b in ranges)
    # (Unicorn puts a hook into code as it translates it: flush what it has)
    if counted and hook is None:
        hook = uc.hook_add(UC_HOOK_BLOCK, on_block)
        uc.ctl_flush_tb()
    elif not counted and hook is not None:
        uc.hook_del(hook)
        hook = None
        uc.ctl_flush_tb()
    state.update(cycles=0, fall=None, cond=False)
    uc.emu_start(pc, mark)
    pc = uc.reg_read(UC_ARM_REG_LR) | 1     # bench_mark returns
    if counted:
        res.append((n, state['cycles']))
        print('list %5d  %9d cycles  %5.2f ms  %3.0f%%' % (n, state['cycles'], state['cycles'] / 150e3, 100 * state['cycles'] / LIST_BUDGET), flush=True)
cyc = [c for n, c in res]
print('lists %d: mean %.2f ms, worst %.2f ms (list %d); a list has 16.77 ms' % (
    len(cyc), sum(cyc) / len(cyc) / 150e3, max(cyc) / 150e3, res[cyc.index(max(cyc))][0]))
if profile:
    tot = sum(per_func.values())
    for f, c in sorted(per_func.items(), key=lambda x: -x[1])[:25]:
        print('%-28s %5.1f%%  %6.2f ms a list' % (f, 100 * c / tot, c / len(cyc) / 150e3))
if hot:
    for (a, n), c in sorted(per_block.items(), key=lambda x: -x[1])[:6]:
        print('--- %08x  %.2f ms a list' % (a, c / len(cyc) / 150e3))
        off = a - code_base
        for i in cs.disasm(code_bytes[off:off + n], a):
            print('    %08x %-8s %s' % (i.address, i.mnemonic, i.op_str))
