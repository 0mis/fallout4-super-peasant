"""Find Caprica temp clobbers in dumped PEX assembly (.pas from Caprica --dump-asm).

Caprica v0.3.0 can hand a temp (::tempN) that still holds a pending value (typically the result of a short-circuit
&& / || or a NOT used as a call argument) to a later sub-expression of the same statement. The later write
overwrites the pending value before it is read, so the call receives the wrong argument (seen in Runner T7: the
pass flag of Result(..., rec && !everDead, "... bleed=" + sp.IsBleedingOut()) became IsBleedingOut()).

Detection: linear dead-store scan per function. A temp written by a computation (ASSIGN, NOT, COMPARE*, arithmetic,
CAST, STRCAT, PROPGET, ARRAY*...) and then written again with no read in between is flagged (the first value is lost).
Unread CALL results are discarded return values and are not flagged. Pending writes are forgotten at an unconditional
JUMP / RETURN (the fall-through path ends there), which avoids if/else false positives.

Make the .pas files with Caprica's --dump-asm (same flags as harness\\build_all.ps1 Caprica-Step). L0 step "clobber"
(test\\run_offline.py) does that for every runtime and test script into work\\integrator\\l0\\asm and runs this checker.
Exit code 1 when anything is flagged. Workaround in the source: put the computed argument into a local variable first
(bool ok = a && b) and pass the variable. (Written by the fix2 flight owner as work\\fix2\\pas_clobber.py; it flagged
exactly the four Runner verdicts of run 20261009-044252 and nothing in the runtime scripts.)

usage: python pas_clobber.py <file.pas | dir> ...      last line: CLOBBERS=<n>
"""
import os
import re
import sys

# opcode -> (indexes of destination operands, indexes of source operands); None = all remaining are sources
DEST = {
    'ASSIGN': [0], 'CAST': [0], 'NOT': [0], 'INEG': [0], 'FNEG': [0],
    'IADD': [0], 'FADD': [0], 'ISUB': [0], 'FSUB': [0], 'FSUBTRACT': [0], 'ISUBTRACT': [0],
    'IMUL': [0], 'FMUL': [0], 'IMULTIPLY': [0], 'FMULTIPLY': [0], 'IDIV': [0], 'FDIV': [0],
    'IDIVIDE': [0], 'FDIVIDE': [0], 'IMOD': [0], 'STRCAT': [0],
    'COMPAREEQ': [0], 'COMPARELT': [0], 'COMPARELTE': [0], 'COMPAREGT': [0], 'COMPAREGTE': [0],
    'CALLMETHOD': [2], 'CALLPARENT': [1], 'CALLSTATIC': [2],
    'PROPGET': [2], 'ARRAYCREATE': [0], 'ARRAYLENGTH': [0], 'ARRAYGETELEMENT': [0],
    'ARRAYFINDELEMENT': [1], 'ARRAYRFINDELEMENT': [1], 'IS': [0], 'STRUCTCREATE': [0], 'STRUCTGET': [0],
    'ARRAYFINDSTRUCT': [1], 'ARRAYRFINDSTRUCT': [1],
}
# operands that are names, not values (never reads)
NAMES = {
    'CALLMETHOD': [0], 'CALLPARENT': [0], 'CALLSTATIC': [0, 1], 'PROPGET': [0], 'PROPSET': [0],
    'STRUCTGET': [2], 'STRUCTSET': [1], 'ARRAYFINDSTRUCT': [2], 'ARRAYRFINDSTRUCT': [2],
    'JUMP': [0], 'JUMPT': [1], 'JUMPF': [1],
}
TOK = re.compile(r'"(?:[^"\\]|\\.)*"|\S+')


def scan(path):
    out = []
    fn = None
    in_code = False
    pending = {}
    for ln, raw in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        line = raw.split(';@line')[0]
        src_line = ''
        m = re.search(r';@line (\d+)', raw)
        if m:
            src_line = m.group(1)
        s = line.strip()
        if s.startswith('.function '):
            fn = s.split()[1]
        elif s == '.code':
            in_code = True
            pending = {}
        elif s == '.endCode':
            in_code = False
        if not in_code or not s or s.startswith('.') or s.endswith(':'):
            continue
        toks = TOK.findall(s)
        op, ops = toks[0].upper(), toks[1:]
        dests = DEST.get(op, [])
        names = NAMES.get(op, [])
        for i, o in enumerate(ops):
            if i in dests or i in names:
                continue
            if o.startswith('::temp'):
                pending.pop(o, None)
        for i in dests:
            if i < len(ops) and ops[i].startswith('::temp'):
                t = ops[i]
                if t in pending:
                    pln, psrc, ptext = pending[t]
                    # an unread CALL result is a discarded return value (a call statement), not a lost argument
                    if not ptext.split()[0].upper().startswith('CALL'):
                        out.append((path, fn, t, psrc, ptext, src_line, s, pln, ln))
                pending[t] = (ln, src_line, s)
        if op in ('JUMP', 'RETURN'):
            pending = {}
    return out


def main(args):
    files = []
    for a in args:
        if os.path.isdir(a):
            for r, _, fs in os.walk(a):
                files += [os.path.join(r, f) for f in fs if f.endswith('.pas')]
        else:
            files.append(a)
    total = 0
    for f in sorted(files):
        for (path, fn, t, psrc, ptext, src_line, s, pln, ln) in scan(f):
            total += 1
            print(f'{os.path.basename(path)} {fn} {t}: value from src line {psrc} [{ptext}] (pas {pln}) '
                  f'overwritten unread by src line {src_line} [{s}] (pas {ln})')
    print(f'CLOBBERS={total}')
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
