"""Finds code only reached through data pointers, vtables or lis/addi pairs that
ReXGlue v0.10.0 did not register as functions, classifies each candidate and
writes the HIGH-confidence ones to config/<game>/discovered-functions.toml.

  python tools/find-missing-functions.py --game mua1-gold               report only
  python tools/find-missing-functions.py --game mua1 --apply-high       also rewrite the TOML
  python tools/find-missing-functions.py --game mua1 --verify           check generated output
  python tools/find-missing-functions.py --game mua1 --self-test        check the decoder and the known manual cases

--game picks mua1 or mua1-gold (default). It needs a codegen run (generated/<game>) and the decompressed image
(build/dump_image.exe game/<folder> default.xex game/analysis/<image>):
  mua1-gold  game/extracted    default.image.bin
  mua1       game/mua1         mua1.image.bin
Method ported from the Hard Corps: Uprising recompilation project.
"""
import bisect
import hashlib
import json
import os
import re
import struct
import sys
from collections import defaultdict
from dataclasses import dataclass, field

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

BASE = 0x82000000
# Sections of each supported default.xex image. CODE is .text without its import thunks, then PSFD00.
GAMES = {
    'mua1-gold': dict(
        image='default.image.bin', size=0xD00000,
        sha256='92ea21814b6d2f818d042b562bcac2532c3a6c3bb78ff30c59569786b3498c51',
        rdata=(0x82000400, 0x820E6570), pdata=(0x820E6600, 0x821152F8),
        code=((0x82120000, 0x8287A480), (0x8287B200, 0x82880ECC)), data=(0x82890000, 0x82C45C5C),
        manual_only={0x82543C70}),   # starts after a null pad inside a region GapFill merged: a hand-made boundary
    'mua1': dict(
        image='mua1.image.bin', size=0xCB0000,
        sha256='b82fcd8cf3fbc879d35aae8ab312385b39ca65e2b2d47da706b92f34b3c08c75',
        rdata=(0x82000400, 0x820D9F60), pdata=(0x820DA000, 0x82108A98),
        code=((0x82110000, 0x8284AF70), (0x8284BC00, 0x828518CC)), data=(0x82860000, 0x82C0E89C),
        manual_only=set()),
}


def select_game(name):
    global IMAGE_PATH, IMAGE_SIZE, IMAGE_SHA256, GEN, OVERRIDES, DISCOVERED, REPORT
    global RDATA, PDATA, CODE_RANGES, DATA, MANUAL_ONLY
    game = GAMES[name]
    IMAGE_PATH = os.path.join(ROOT, 'game', 'analysis', game['image'])
    IMAGE_SIZE, IMAGE_SHA256 = game['size'], game['sha256']
    GEN = os.path.join(ROOT, 'generated', name)
    OVERRIDES = os.path.join(ROOT, 'config', name, 'overrides.toml')
    DISCOVERED = os.path.join(ROOT, 'config', name, 'discovered-functions.toml')
    REPORT = os.path.join(ROOT, 'build', f'missing-functions-report-{name}.txt')
    RDATA, PDATA, CODE_RANGES, DATA = game['rdata'], game['pdata'], game['code'], game['data']
    MANUAL_ONLY = game['manual_only']


select_game('mua1-gold')
CXX_EH_MAGIC = 0x19930520
MAX_HIGH_BYTES = 0x400

IMG = b''
table_sites = defaultdict(list)   # lis/addi-built .rdata/.data address -> code sites


def u32(a):
    return struct.unpack_from('>I', IMG, a - BASE)[0]


def in_code(a):
    return (a & 3) == 0 and any(lo <= a < hi for lo, hi in CODE_RANGES)


def code_end(a):
    return next(hi for lo, hi in CODE_RANGES if lo <= a < hi)


def code_words():
    for lo, hi in CODE_RANGES:
        yield from range(lo, hi, 4)


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


# ---------------------------------------------------------------------------
# Minimal PowerPC decoder: enough to classify control flow and reject data.
# ---------------------------------------------------------------------------
XO19 = {0, 16, 18, 33, 50, 129, 150, 193, 225, 257, 274, 289, 417, 449, 528}
XO31 = {0, 4, 8, 9, 10, 11, 19, 20, 21, 23, 24, 26, 27, 28, 32, 40, 53, 54, 55, 58, 60, 68, 73, 75,
        83, 84, 86, 87, 104, 119, 124, 136, 138, 144, 146, 149, 150, 151, 178, 181, 183, 200, 202,
        210, 214, 215, 232, 233, 234, 235, 246, 247, 266, 278, 279, 284, 306, 310, 311, 316, 339,
        341, 343, 370, 371, 373, 375, 407, 412, 434, 438, 439, 444, 457, 459, 467, 470, 476, 489,
        491, 498, 512, 533, 534, 535, 536, 539, 566, 567, 595, 597, 598, 599, 631, 659, 661, 662,
        663, 695, 725, 727, 759, 790, 792, 794, 824, 826, 827, 854, 918, 922, 954, 982, 983, 986,
        1014,
        6, 7, 38, 39, 71, 103, 135, 167, 199, 231, 342, 359, 374, 487, 519, 551, 647, 679, 775,
        807, 822, 903, 935}
XO59 = {18, 20, 21, 22, 24, 25, 26, 28, 29, 30, 31}
XO63_A = {18, 20, 21, 22, 23, 24, 25, 26, 28, 29, 30, 31}
XO63_X = {0, 12, 14, 15, 32, 38, 40, 64, 70, 72, 136, 264, 583, 711, 814, 815, 846, 974}
PRIMARY_SIMPLE = {2, 3, 7, 8, 10, 11, 12, 13, 14, 15, 17, 20, 21, 23, 24, 25, 26, 27, 28, 29, 32,
                  33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
                  53, 54, 55}


@dataclass
class Insn:
    addr: int
    word: int
    kind: str          # b, bl, bc, bcl, blr, bclr, bctr, bctrl, bcctr, call, other, unknown, null
    target: int = 0


def decode(a):
    w = u32(a)
    if w == 0:
        return Insn(a, w, 'null')
    op = w >> 26
    if op == 18:
        target = sext(w & 0x03FFFFFC, 26)
        target = target if w & 2 else (a + target) & 0xFFFFFFFF
        return Insn(a, w, 'bl' if w & 1 else 'b', target)
    if op == 16:
        bo = (w >> 21) & 31
        target = sext(w & 0xFFFC, 16)
        target = target if w & 2 else (a + target) & 0xFFFFFFFF
        cond = (bo & 0x14) != 0x14
        if w & 1:
            return Insn(a, w, 'bcl', target)
        return Insn(a, w, 'bc' if cond else 'b', target)
    if op == 19:
        xo = (w >> 1) & 0x3FF
        if xo not in XO19:
            return Insn(a, w, 'unknown')
        bo = (w >> 21) & 31
        cond = (bo & 0x14) != 0x14
        if xo == 16:
            if w & 1:
                return Insn(a, w, 'call')
            return Insn(a, w, 'bclr' if cond else 'blr')
        if xo == 528:
            if w & 1:
                return Insn(a, w, 'bctrl')
            return Insn(a, w, 'bcctr' if cond else 'bctr')
        return Insn(a, w, 'other')
    if op in PRIMARY_SIMPLE or op in (4, 5, 6):
        return Insn(a, w, 'other')
    if op == 30:
        return Insn(a, w, 'other' if ((w >> 2) & 7) <= 4 or ((w >> 1) & 15) in (8, 9) else 'unknown')
    if op == 31:
        return Insn(a, w, 'other' if ((w >> 1) & 0x3FF) in XO31 else 'unknown')
    if op == 58:
        return Insn(a, w, 'other' if (w & 3) <= 2 else 'unknown')
    if op == 59:
        return Insn(a, w, 'other' if ((w >> 1) & 0x1F) in XO59 else 'unknown')
    if op == 62:
        return Insn(a, w, 'other' if (w & 3) <= 1 else 'unknown')
    if op == 63:
        xo5 = (w >> 1) & 0x1F
        xo10 = (w >> 1) & 0x3FF
        return Insn(a, w, 'other' if xo5 in XO63_A or xo10 in XO63_X else 'unknown')
    return Insn(a, w, 'unknown')


TERMINATORS = {'b', 'blr', 'bctr'}


def is_terminator_word(a):
    return decode(a).kind in TERMINATORS


# ---------------------------------------------------------------------------
# Inputs from the previous codegen run and the config files
# ---------------------------------------------------------------------------
@dataclass
class Known:
    starts: list                      # sorted registered function starts
    start_set: set
    imports: set
    labels: set                       # loc_ labels inside generated functions
    pdata: dict                       # begin -> (end, exception flag)
    pdata_begins: list
    config: dict                      # start -> end for existing [functions] entries
    branch_sites: dict                # target -> [sites] for direct b/bl/bc in code


def parse_config_functions(path):
    entries = {}
    if not os.path.exists(path):
        return entries
    for m in re.finditer(r'^\s*(0x[0-9A-Fa-f]{8})\s*=\s*\{([^}]*)\}', open(path).read(), re.M):
        start = int(m.group(1), 16)
        body = m.group(2)
        end = re.search(r'end\s*=\s*(0x[0-9A-Fa-f]+)', body)
        size = re.search(r'size\s*=\s*(0x[0-9A-Fa-f]+|\d+)', body)
        if end:
            entries[start] = int(end.group(1), 16)
        elif size:
            entries[start] = start + int(size.group(1), 0)
        else:
            entries[start] = start + 4
    return entries


def load_known():
    register = open(os.path.join(GEN, 'marvel_ultimate_alliance_register.cpp')).read()
    starts, imports = set(), set()
    for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}), (\w+)\)', register):
        a = int(m.group(1), 16)
        starts.add(a)
        if m.group(2).startswith('__imp__'):
            imports.add(a)
    partition = json.load(open(os.path.join(GEN, 'codegen.partition.json')))['assignments']
    missing = {int(k, 16) for k in partition} - starts
    if missing:
        sys.exit(f'partition/register mismatch: {len(missing)} functions not registered')

    labels = set()
    for f in sorted(os.listdir(GEN)):
        if '_recomp.' in f and f.endswith('.cpp'):
            labels.update(int(x, 16) for x in re.findall(r'^loc_([0-9A-F]{8}):', open(os.path.join(GEN, f)).read(), re.M))

    pdata = {}
    for a in range(PDATA[0], PDATA[1], 8):
        begin, data = u32(a), u32(a + 4)
        if begin == 0:
            continue
        length = (data >> 8) & 0x3FFFFF
        pdata[begin] = (begin + max(length, 1) * 4, bool(data >> 31))

    # Entries this tool generated earlier are re-derived from the image on every
    # run, so they are scanned as candidates again rather than taken as known.
    generated = parse_config_functions(DISCOVERED)
    starts -= set(generated)
    labels = {l for l in labels if not any(s <= l < e for s, e in generated.items())}
    config = parse_config_functions(OVERRIDES)

    sites = defaultdict(list)
    for a in code_words():
        insn = decode(a)
        if insn.kind in ('b', 'bl', 'bc', 'bcl'):
            sites[insn.target].append(a)

    return Known(sorted(starts), starts, imports, labels, pdata, sorted(pdata), config, sites)


# ---------------------------------------------------------------------------
# Exception-handling tables in .rdata (the SDK registers their funclets itself)
# ---------------------------------------------------------------------------
def eh_table_words(known):
    words = set()

    def mark(start, size):
        words.update(range(start, start + size, 4))

    def in_rdata(a, size=4):
        return RDATA[0] <= a and a + size <= RDATA[1]

    for begin, (_, has_eh) in known.pdata.items():
        if not has_eh or begin - 8 < CODE_RANGES[0][0]:
            continue
        table = u32(begin - 4)
        if not in_rdata(table, 28):
            continue
        if u32(table) == CXX_EH_MAGIC:
            max_state, unwind, n_try, try_map, n_ip, ip_map = (u32(table + o) for o in (4, 8, 12, 16, 20, 24))
            mark(table, 28)
            if unwind and in_rdata(unwind, max_state * 8):
                mark(unwind, max_state * 8)
            if try_map and in_rdata(try_map, n_try * 20):
                mark(try_map, n_try * 20)
                for i in range(n_try):
                    n_catch, handlers = u32(try_map + i * 20 + 12), u32(try_map + i * 20 + 16)
                    if handlers and n_catch <= 20 and in_rdata(handlers, n_catch * 16):
                        mark(handlers, n_catch * 16)
            if ip_map and in_rdata(ip_map, n_ip * 8):
                mark(ip_map, n_ip * 8)
        else:
            count = u32(table)
            if count <= 0x1000 and in_rdata(table, 4 + count * 16):
                mark(table, 4 + count * 16)
    return words


# ---------------------------------------------------------------------------
# References: data pointers and lis/addi pairs that name code addresses
# ---------------------------------------------------------------------------
@dataclass
class Ref:
    site: int
    kind: str      # vtable, dataptr, lisaddi, tailcall, eh, jumptable
    section: str


def pointer_run(a):
    lo = a
    while in_code(u32(lo - 4)):
        lo -= 4
    hi = a
    while in_code(u32(hi + 4)):
        hi += 4
    return lo, hi


def collect_refs(known, eh_words):
    refs = defaultdict(list)
    for lo, hi, section in ((RDATA[0], RDATA[1], '.rdata'), (DATA[0], DATA[1], '.data')):
        for a in range(lo, hi, 4):
            t = u32(a)
            if not in_code(t):
                continue
            if a in eh_words:
                kind = 'eh'
            else:
                run_lo, run_hi = pointer_run(a)
                entries = [u32(x) for x in range(run_lo, run_hi + 4, 4)]
                if any(e in known.labels for e in entries):
                    kind = 'jumptable'
                elif run_hi > run_lo:
                    kind = 'vtable'
                else:
                    kind = 'dataptr'
            refs[t].append(Ref(a, kind, section))

    lis = [None] * 32
    for a in code_words():
        w = u32(a)
        op, rd, ra = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        if op == 15 and ra == 0:
            lis[rd] = (w & 0xFFFF) << 16
            continue
        if op in (14, 24) and lis[ra] is not None:
            lo = w & 0xFFFF
            t = (lis[ra] + sext(lo, 16)) & 0xFFFFFFFF if op == 14 else lis[ra] | lo
            if in_code(t):
                refs[t].append(Ref(a, 'lisaddi', '.text'))
            elif RDATA[0] <= t < RDATA[1] or DATA[0] <= t < DATA[1]:
                table_sites[t].append(a)
        if is_terminator_word(a):
            lis = [None] * 32

    for a in code_words():
        insn = decode(a)
        t = insn.target
        if insn.kind != 'b' or not in_code(t) or t in known.labels:
            continue
        if t in known.start_set and t not in known.config:
            continue
        i = bisect.bisect_right(known.starts, a) - 1
        low = known.starts[i] if i >= 0 else 0
        high = known.starts[i + 1] if i + 1 < len(known.starts) else CODE_RANGES[-1][1]
        if not low <= t < high:
            refs[t].append(Ref(a, 'tailcall', '.text'))
    return refs


def switch_dispatch_follows(site):
    """True when the address built at site feeds an indexed load/add and a bctr soon after."""
    indexed = False
    for a in range(site + 4, site + 36, 4):
        w = u32(a)
        if (w >> 26) == 31 and ((w >> 1) & 0x3FF) in (23, 87, 279, 266):
            indexed = True
        if w == 0x4E800420:
            return indexed
        if decode(a).kind in TERMINATORS:
            return False
    return False


def jump_table_site(t, refs, known):
    """Site in the preceding registered function that uses the referencing table as a switch table."""
    i = bisect.bisect_left(known.starts, t) - 1
    prev = known.starts[i] if i >= 0 else 0
    for r in refs:
        if r.kind not in ('vtable', 'dataptr'):
            continue
        lo, hi = pointer_run(r.site)
        for table in range(lo, hi + 4, 4):
            for site in table_sites.get(table, ()):
                if prev <= site < t and switch_dispatch_follows(site):
                    return site
    return 0


# ---------------------------------------------------------------------------
# Control-flow walk of one candidate function
# ---------------------------------------------------------------------------
@dataclass
class Walk:
    ok: bool = True
    reasons: list = field(default_factory=list)
    visited: set = field(default_factory=set)
    insns: dict = field(default_factory=dict)
    terminators: list = field(default_factory=list)
    has_call: bool = False

    @property
    def end(self):
        return max(self.visited) + 4 if self.visited else 0


def walk(start, bound, known, extra_starts=frozenset(), own_labels=False):
    """Linear-sweep CFG walk bounded by [start, bound). A registered function's own
    loc_ labels are extra block starts; for a candidate every label is foreign."""
    callable_ = lambda t: t in known.start_set or t in known.imports or t in extra_starts
    w = Walk()
    worklist, queued = [start], {start}
    if own_labels:
        for l in known.labels:
            if start < l < bound:
                queued.add(l)
                worklist.append(l)

    def fail(reason):
        w.ok = False
        if reason not in w.reasons:
            w.reasons.append(reason)

    def push(t):
        if t not in w.visited and t not in queued:
            queued.add(t)
            worklist.append(t)

    while worklist:
        a = worklist.pop()
        while True:
            if a >= bound:
                fail('falls into next function')
                break
            if a in w.visited:
                break
            if a != start and a in known.labels and not own_labels:
                fail(f'reaches label {a:08X} of another function')
                break
            insn = decode(a)
            w.visited.add(a)
            w.insns[a] = insn
            k = insn.kind
            if k == 'null':
                fail(f'null word at {a:08X}')
                break
            if k == 'unknown':
                fail(f'undecodable word {insn.word:08X} at {a:08X}')
                break
            if k == 'b':
                if start <= insn.target < bound:
                    push(insn.target)
                elif callable_(insn.target):
                    w.terminators.append(insn)
                else:
                    fail(f'b to unknown {insn.target:08X}')
                break
            if k in ('bl', 'bcl'):
                w.has_call = True
                if insn.target != start and not callable_(insn.target):
                    fail(f'bl to unknown {insn.target:08X}')
                    break
            elif k == 'bc':
                if start <= insn.target < bound:
                    push(insn.target)
                elif not callable_(insn.target):
                    fail(f'bc to unknown {insn.target:08X}')
                    break
            elif k in ('blr', 'bctr'):
                w.terminators.append(insn)
                break
            elif k in ('bctrl', 'call'):
                w.has_call = True
            a += 4
    if not w.terminators and w.ok:
        fail('no terminator')
    if w.ok and w.visited and (max(w.visited) + 4 > bound):
        fail('falls into next function')
    return w


# ---------------------------------------------------------------------------
# Candidate classification
# ---------------------------------------------------------------------------
@dataclass
class Candidate:
    start: int
    end: int = 0
    confidence: str = 'LOW'
    ref: int = 0
    ref_kind: str = ''
    prev: int = 0
    prev_term: str = ''
    final_term: str = ''
    shape: str = ''
    reasons: list = field(default_factory=list)


def shape_of(w):
    insns = [w.insns[a] for a in sorted(w.insns)]
    n = len(insns)
    terms = {t.kind for t in w.terminators}
    words = [i.word for i in insns]
    if n == 2 and (words[0] >> 16) == 0x3863 and insns[1].kind == 'b':
        return 'this-adjust-thunk'
    if terms == {'b'} and not w.has_call and n <= 8:
        return 'tailcall-thunk'
    if 'bctr' in terms and not w.has_call and any(x >> 16 == 0x7D69 and x & 0xFFFF == 0x03A6 for x in words) and n <= 12:
        return 'guarded-vcall-thunk' if any(i.kind == 'bc' for i in insns) else 'vcall-thunk'
    if 'bctr' in terms and not w.has_call:
        return 'indirect-tail'
    if not w.has_call and terms == {'blr'}:
        return 'leaf-small' if n <= 8 else 'leaf'
    if not w.has_call:
        return 'leaf-tail'
    return 'normal'


def previous_entity(t, known, accepted):
    """Return (start, kind, end) of the function or accepted candidate before t."""
    i = bisect.bisect_left(known.starts, t) - 1
    prev_known = known.starts[i] if i >= 0 else 0
    prev_acc = max((s for s in accepted if s < t), default=0)
    if prev_acc > prev_known:
        return prev_acc, 'candidate', accepted[prev_acc]
    if prev_known in known.config:
        return prev_known, 'candidate', known.config[prev_known]
    if prev_known in known.pdata:
        return prev_known, 'pdata', known.pdata[prev_known][0]
    return prev_known, 'known', 0


def pdata_extent_containing(t, known):
    i = bisect.bisect_right(known.pdata_begins, t) - 1
    if i >= 0:
        begin = known.pdata_begins[i]
        if begin < t < known.pdata[begin][0]:
            return begin
    return 0


def next_boundary(t, known, all_candidates):
    i = bisect.bisect_right(known.starts, t)
    bound = known.starts[i] if i < len(known.starts) else code_end(t)
    j = bisect.bisect_right(all_candidates, t)
    if j < len(all_candidates):
        bound = min(bound, all_candidates[j])
    for s, e in known.config.items():
        if s > t:
            bound = min(bound, s)
    return bound


def classify(t, refs, known, accepted, all_candidates):
    c = Candidate(t)
    kinds = {r.kind for r in refs}
    useful = [r for r in refs if r.kind in ('vtable', 'dataptr', 'lisaddi', 'tailcall')]
    primary = useful[0] if useful else refs[0]
    c.ref, c.ref_kind = primary.site, primary.kind
    if len(refs) > 1:
        c.ref_kind += f'x{len(refs)}'

    prev_insn = decode(t - 4)
    c.prev_term = prev_insn.kind
    prev, prev_kind, prev_end = previous_entity(t, known, accepted)
    c.prev = prev

    bound = next_boundary(t, known, all_candidates)
    w = walk(t, bound, known, frozenset(accepted))
    c.end = w.end if w.visited else t + 4
    c.final_term = '/'.join(sorted({x.kind for x in w.terminators})) or '-'
    c.shape = shape_of(w) if w.ok else '-'

    hard, soft = [], []
    if prev_insn.kind not in TERMINATORS:
        hard.append(f'previous word is {prev_insn.kind}')
    inside = pdata_extent_containing(t, known)
    if inside:
        hard.append(f'inside .pdata extent of {inside:08X}')
    if 'jumptable' in kinds:
        hard.append('referenced from a jump table')
    jt_site = jump_table_site(t, refs, known)
    if jt_site:
        hard.append(f'table used as switch table at {jt_site:08X}')
    if not w.ok:
        hard.extend(w.reasons)
    if not useful:
        hard.append('eh-only' if kinds == {'eh'} else 'no usable reference')

    if w.ok:
        for a in range(t + 4, c.end, 4):
            outside = [s for s in known.branch_sites.get(a, ()) if not t <= s < c.end]
            if outside:
                hard.append(f'branch from {outside[0]:08X} enters {a:08X}')
                break
        # Unreached words between the walked end and the next boundary.
        tail = []
        a = c.end
        while a < bound and u32(a) != 0:
            tail.append(decode(a).kind if u32(a) != 0x60000000 else 'nop')
            a += 4
        if tail and all(k in ('blr', 'nop') for k in tail) and len(tail) <= 2:
            c.end = a
        elif tail:
            soft.append(f'{len(tail)} unreached words after end')
        if c.end - t > MAX_HIGH_BYTES:
            soft.append('large body')

    if prev_kind == 'pdata':
        if prev_end > t:
            hard.append(f'inside .pdata extent of {prev:08X}')
        elif prev_end < t:
            soft.append(f'gap after .pdata end {prev_end:08X}')
    elif prev_kind == 'candidate':
        if prev_end != t:
            soft.append(f'gap after candidate {prev:08X}')
    else:
        pw = walk(prev, bound, known, frozenset(accepted), own_labels=True)
        if pw.visited and max(pw.visited) >= t:
            hard.append(f'inside walked body of {prev:08X}')
        elif not pw.ok or not pw.visited:
            soft.append(f'previous function {prev:08X} not walkable')
        elif pw.end != t:
            soft.append(f'gap after {prev:08X} end {pw.end:08X}')

    for s, e in known.config.items():
        if s < c.end and t < e:
            hard.append(f'overlaps configured {s:08X}')

    if hard:
        c.confidence = 'EH' if 'eh-only' in hard else 'LOW'
        c.reasons = hard
    elif soft:
        c.confidence = 'MEDIUM'
        c.reasons = soft
    else:
        c.confidence = 'HIGH'
        c.reasons = ['clean']
    return c


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
def write_report(cands, calibration, stats):
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    lines = ['# ' + ' '.join(f'{k}={v}' for k, v in stats.items()),
             '# START END CONFIDENCE REFERENCE REFTYPE PREV PREVTERM FINALTERM SHAPE REASON']
    for c in cands:
        lines.append(f'{c.start:08X} {c.end:08X} {c.confidence} {c.ref:08X} {c.ref_kind} {c.prev:08X} '
                     f'{c.prev_term} {c.final_term} {c.shape} {"; ".join(c.reasons)}')
    lines.append('# calibration: existing config entries re-classified as if unregistered')
    for c, configured_end in calibration:
        match = 'end-match' if c.end == configured_end else f'end-differs(config {configured_end:08X})'
        lines.append(f'{c.start:08X} {c.end:08X} {c.confidence} {c.ref:08X} {c.ref_kind} {c.prev:08X} '
                     f'{c.prev_term} {c.final_term} {c.shape} {match}; {"; ".join(c.reasons)}')
    open(REPORT, 'w').write('\n'.join(lines) + '\n')


def write_discovered(cands):
    lines = ['# Generated by tools/find-missing-functions.py --apply-high; do not edit by hand.', '[functions]']
    for c in cands:
        lines.append(f'0x{c.start:08X} = {{ end = 0x{c.end:08X} }}')
    open(DISCOVERED, 'w', newline='\n').write('\n'.join(lines) + '\n')


def verify():
    entries = parse_config_functions(DISCOVERED)
    register = open(os.path.join(GEN, 'marvel_ultimate_alliance_register.cpp')).read()
    registered = defaultdict(int)
    for m in re.finditer(r'SetFunction\(0x([0-9A-F]{8}),', register):
        registered[int(m.group(1), 16)] += 1
    defined = defaultdict(int)
    for f in os.listdir(GEN):
        if '_recomp.' in f and f.endswith('.cpp'):
            for m in re.finditer(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)', open(os.path.join(GEN, f)).read(), re.M):
                defined[int(m.group(1), 16)] += 1
    starts = sorted(registered)
    failures = 0
    for s, e in sorted(entries.items()):
        problems = []
        if registered[s] != 1:
            problems.append(f'registered {registered[s]} times')
        if defined[s] != 1:
            problems.append(f'defined {defined[s]} times')
        i = bisect.bisect_right(starts, s)
        if i < len(starts) and starts[i] < e:
            problems.append(f'function {starts[i]:08X} starts inside')
        if problems:
            failures += 1
            print(f'FAIL 0x{s:08X}-0x{e:08X}: {"; ".join(problems)}')
    print(f'verify: {len(entries)} entries, {failures} failures, {len(registered)} registered functions')
    return failures == 0


def jump_table_leaks(cands, refs, known):
    leaks = []
    for c in cands:
        if c.confidence != 'HIGH':
            continue
        for r in refs[c.start]:
            if r.kind in ('vtable', 'dataptr') and any(u32(x) in known.labels for x in range(*pointer_run_bounds(r.site))):
                leaks.append(c.start)
    return leaks


def pointer_run_bounds(site):
    lo, hi = pointer_run(site)
    return lo, hi + 4, 4


def self_test(cands, calibration, leaks):
    words = {0x48000009: 'bl', 0x48000008: 'b', 0x4E800020: 'blr', 0x4E800420: 'bctr', 0x4E800421: 'bctrl',
             0x4D820020: 'bclr', 0x00000000: 'null', 0x7D6903A6: 'other', 0x3863000C: 'other', 0x419A0014: 'bc'}
    failures = [f'{w:08X}: {decode_word(w)} != {k}' for w, k in words.items() if decode_word(w) != k]
    for c, configured_end in calibration:
        if c.end != configured_end:
            failures.append(f'{c.start:08X}: end {c.end:08X} != configured {configured_end:08X}')
        if c.start not in MANUAL_ONLY and c.confidence != 'HIGH':
            failures.append(f'{c.start:08X}: calibration case is {c.confidence} ({"; ".join(c.reasons)})')
        if c.start in MANUAL_ONLY and c.confidence == 'HIGH':
            failures.append(f'{c.start:08X}: manual case unexpectedly HIGH')
    failures.extend(f'{t:08X}: HIGH candidate is referenced from a jump table' for t in leaks)
    if any(c.confidence == 'HIGH' and c.reasons != ['clean'] for c in cands):
        failures.append('HIGH candidate with reasons')
    for failure in failures:
        print('SELF-TEST FAIL', failure)
    print(f'self-test: {len(calibration)} calibration cases, {len(failures)} failures')
    return not failures


def decode_word(word):
    global IMG
    saved = IMG
    IMG = word.to_bytes(4, 'big') * 2
    try:
        return decode(BASE).kind
    finally:
        IMG = saved



def main(argv):
    global IMG
    apply_high = '--apply-high' in argv
    if '--game' in argv:
        name = argv[argv.index('--game') + 1]
        if name not in GAMES:
            sys.exit(f'unknown --game {name}: {", ".join(GAMES)}')
        select_game(name)
    IMG = open(IMAGE_PATH, 'rb').read()
    if len(IMG) != IMAGE_SIZE:
        sys.exit(f'{IMAGE_PATH}: unexpected size 0x{len(IMG):X}')
    if hashlib.sha256(IMG).hexdigest() != IMAGE_SHA256:
        sys.exit(f'{IMAGE_PATH}: not the supported image dump')
    if '--verify' in argv:
        return 0 if verify() else 1

    known = load_known()
    eh_words = eh_table_words(known)
    refs = collect_refs(known, eh_words)

    configured = set(known.config)
    targets = sorted(t for t in refs if t not in known.start_set and t not in known.labels and t not in configured)
    accepted = {}
    cands = []
    for t in targets:
        c = classify(t, refs[t], known, accepted, targets)
        cands.append(c)
        if c.confidence == 'HIGH':
            accepted[t] = c.end

    calibration = []
    saved_config, saved_labels = dict(known.config), set(known.labels)
    for s, e in sorted(saved_config.items()):
        known.config = {k: v for k, v in saved_config.items() if k != s}
        known.labels = {l for l in saved_labels if not s <= l < e}
        ref_list = refs.get(s) or [Ref(0, 'none', '-')]
        calibration.append((classify(s, ref_list, known, {}, targets), e))
    known.config, known.labels = saved_config, saved_labels

    counts = defaultdict(int)
    for c in cands:
        counts[c.confidence] += 1
    stats = {'references': sum(len(v) for v in refs.values()), 'targets': len(refs),
             'unregistered': len(targets), 'HIGH': counts['HIGH'], 'MEDIUM': counts['MEDIUM'],
             'LOW': counts['LOW'], 'EH': counts['EH'], 'configured': len(saved_config)}
    write_report(cands, calibration, stats)
    print(' '.join(f'{k}={v}' for k, v in stats.items()))
    print(f'report: {REPORT}')
    if '--self-test' in argv:
        return 0 if self_test(cands, calibration, jump_table_leaks(cands, refs, known)) else 1

    high = [c for c in cands if c.confidence == 'HIGH']
    if apply_high:
        write_discovered(high)
        print(f'wrote {len(high)} entries to {DISCOVERED}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
