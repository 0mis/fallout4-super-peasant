"""FO4 3.9 PEX and AMD64 PE metadata sanitizer. Python stdlib only; never edits inputs.

Check-only performs transformations in memory. Verify-dir independently reparses copies.
The executable PEX tail and every DLL byte outside the parsed RSDS path must match.
Format references and root commands are in DESIGN.md. No source strings are printed.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


class Invalid(ValueError):
    pass


def need(ok, message):
    if not ok:
        raise Invalid(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def lp(data):
    need(len(data) <= 65535, "PEX string too long")
    return struct.pack("<H", len(data)) + data


def basename(data):
    value = data.replace(b"\\", b"/").rsplit(b"/", 1)[-1]
    need(value not in (b"", b".", b"..") and b":" not in value, "Invalid metadata basename")
    return value


ABS_PATH = re.compile(rb'(?i)(?:[a-z]:[\\/]|\\\\[a-z0-9_.-]+[\\/]|/(?:Users|home|tmp)/)[^\s\x00()<>"|?*]+')
PRIVATE_PATH = re.compile(rb'(?i)(?:^[a-z]:[\\/]Users[\\/]|[\\/]scratch[\\/]|^/(?:Users|home|tmp)/)')
# FO4 opcodes 0..47, fixed typed-value operand counts. Calls 23..25 have varargs.
ARGS = (0,3,3,3,3,3,3,3,3,3,2,2,2,2,2,3,3,3,3,3,1,2,2,3,2,3,1,3,3,3,2,2,3,3,4,4,3,1,3,3,5,5,3,3,1,3,1,6)


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def take(self, n):
        need(n >= 0 and self.pos + n <= len(self.data), "Truncated binary structure")
        result = self.data[self.pos:self.pos+n]
        self.pos += n
        return result

    def u8(self): return self.take(1)[0]
    def u16(self): return struct.unpack("<H", self.take(2))[0]
    def u32(self): return struct.unpack("<I", self.take(4))[0]
    def string(self): return self.take(self.u16())


class Pex(Reader):
    """Complete FO4-tail walk accounts for every string-index use; unknown data fails."""
    def __init__(self, data):
        super().__init__(data)
        need(self.take(8) == bytes.fromhex("dec057fa03090200"), "Expected FO4 PEX3.9/game2")
        self.take(8)
        self.header = [self.string() for _ in range(3)]
        self.strings = [self.string() for _ in range(self.u16())]
        self.tail = self.pos
        self.uses = [set() for _ in self.strings]
        self.doc_offsets = {}
        self.instructions = 0
        self.walk()
        need(self.pos == len(data), "Unparsed PEX trailing bytes")

    def ref(self, role="semantic"):
        at, index = self.pos, self.u16()
        need(index < len(self.strings), "Invalid PEX string index")
        self.uses[index].add(role)
        if role == "doc":
            self.doc_offsets.setdefault(index, []).append(at - self.tail)
        return index

    def value(self):
        kind = self.u8()
        if kind == 0: return kind, None
        if kind in (1, 2): return kind, self.ref()
        if kind in (3, 4): return kind, self.u32()
        if kind == 5: return kind, self.u8()
        raise Invalid("Unknown PEX value type")

    def function(self):
        self.ref(); self.ref("doc"); self.take(5)
        for _ in range(2):  # parameters, locals
            for _ in range(self.u16()):
                self.ref(); self.ref()
        n = self.u16()
        self.instructions += n
        for _ in range(n):
            op = self.u8()
            need(op < len(ARGS), "Unknown FO4 opcode")
            for _ in range(ARGS[op]): self.value()
            if op in (23, 24, 25):
                kind, count = self.value()
                need(kind == 3 and count <= 65535, "Invalid PEX vararg count")
                for _ in range(count): self.value()

    def walk(self):
        debug = self.u8()
        need(debug in (0, 1), "Invalid PEX debug flag")
        if debug:
            self.take(8)
            for _ in range(self.u16()):
                self.ref(); self.ref(); self.ref(); self.u8()
                self.take(2 * self.u16())
            for _ in range(self.u16()):
                self.ref(); self.ref(); self.ref("doc"); self.u32()
                for _ in range(self.u16()): self.ref()
            for _ in range(self.u16()):
                self.ref(); self.ref()
                for _ in range(self.u16()): self.ref()
        for _ in range(self.u16()): self.ref(); self.u8()  # user flags
        for _ in range(self.u16()):  # objects
            self.ref(); self.u32(); self.ref(); self.ref("doc")
            self.u8(); self.u32(); self.ref()
            for _ in range(self.u16()):  # structs
                self.ref()
                for _ in range(self.u16()):
                    self.ref(); self.ref(); self.u32(); self.value(); self.u8(); self.ref("doc")
            for _ in range(self.u16()):  # variables
                self.ref(); self.ref(); self.u32(); self.value(); self.u8()
            for _ in range(self.u16()):  # properties
                self.ref(); self.ref(); self.ref("doc"); self.u32()
                flags = self.u8()
                need(flags & ~7 == 0, "Unknown PEX property flags")
                if flags & 4: self.ref()
                else:
                    if flags & 1: self.function()
                    if flags & 2: self.function()
            for _ in range(self.u16()):  # states
                self.ref()
                for _ in range(self.u16()): self.ref(); self.function()


def doc_changes(p):
    changes = {}
    for i, data in enumerate(p.strings):
        matches = list(ABS_PATH.finditer(data))
        if not matches:
            continue
        need(p.uses[i] == {"doc"}, "Path-bearing PEX string is not exclusively DOCSTRING: index " + str(i))
        need(all(PRIVATE_PATH.search(m[0]) for m in matches), "Unreviewed absolute DOCSTRING path: index " + str(i))
        changes[i] = ABS_PATH.sub(lambda m: basename(m[0]), data)
    return changes


def sanitize_pex(data):
    p = Pex(data)
    changes = doc_changes(p)
    header = [basename(p.header[0]), b"public", b"public"]
    return data[:16] + b"".join(map(lp, header)) + struct.pack("<H", len(p.strings)) + b"".join(lp(changes.get(i, s)) for i, s in enumerate(p.strings)) + data[p.tail:]


def verify_pex(before, after):
    a, b = Pex(before), Pex(after)
    need(before[:16] == after[:16], "PEX fixed header changed")
    need(b.header == [basename(a.header[0]), b"public", b"public"], "PEX header sanitization mismatch")
    need(len(a.strings) == len(b.strings), "PEX string-table count changed")
    changes = doc_changes(a)
    need(a.uses == b.uses, "PEX string-index uses changed")
    for i, original in enumerate(a.strings):
        need(b.strings[i] == changes.get(i, original), "Unexpected PEX string change at index " + str(i))
    need(before[a.tail:] == after[b.tail:], "PEX executable/debug/object tail changed")
    need(not any(ABS_PATH.search(s) for s in b.header + b.strings), "Absolute path remains in PEX metadata/table")
    def projection(p):
        return p.data[:16] + struct.pack("<H", len(p.strings)) + b"".join(lp(b"" if i in changes else s) for i,s in enumerate(p.strings)) + p.data[p.tail:]
    pa, pb = projection(a), projection(b)
    need(pa == pb, "PEX runtime projection changed")
    return {"format":"FO4 PEX3.9/game2", "strings":len(a.strings), "instructions":a.instructions,
            "header_fields_changed":[i for i in range(3) if a.header[i] != b.header[i]],
            "doc_changes":[{"index":i, "before_sha256":sha(a.strings[i]), "after_sha256":sha(b.strings[i]), "reference_roles":sorted(a.uses[i]), "tail_reference_offsets":a.doc_offsets[i]} for i in changes],
            "tail_before_offset":a.tail, "tail_after_offset":b.tail, "tail_bytes":len(before)-a.tail,
            "tail_sha256":sha(before[a.tail:]), "runtime_projection_sha256":sha(pa), "runtime_unchanged":True}


def pe_metadata(data):
    def span(at, n):
        need(at >= 0 and n >= 0 and at+n <= len(data), "PE range outside file")
        return data[at:at+n]
    def num(fmt, at): return struct.unpack(fmt, span(at, struct.calcsize(fmt)))
    need(span(0,2) == b"MZ", "Expected DOS header")
    pe = num("<I",0x3c)[0]
    need(span(pe,4) == b"PE\0\0", "Expected PE signature")
    machine, count = num("<HH",pe+4)
    opt_size = num("<H",pe+20)[0]; opt = pe+24
    need(machine == 0x8664 and 0 < count <= 96 and opt_size >= 168, "Expected AMD64 PE32+")
    span(opt,opt_size)
    need(num("<H",opt)[0] == 0x20b and num("<I",opt+108)[0] >= 7, "Missing PE32+ debug directory")
    need(num("<II",opt+112+4*8) == (0,0), "Signed PE is outside metadata-only scope")
    need(num("<I",opt+64)[0] == 0, "Nonzero PE checksum is outside this scope")
    sections=[]
    for i in range(count):
        at=opt+opt_size+i*40; row=span(at,40)
        vs,va,size,ptr=struct.unpack_from("<IIII",row,8)
        chars=struct.unpack_from("<I",row,36)[0]
        span(ptr,size)
        sections.append({"name":row[:8].rstrip(b"\0").decode("ascii"),"va":va,"size":size,"ptr":ptr,"chars":chars})
    def rva_offset(rva,size):
        found=[s["ptr"]+rva-s["va"] for s in sections if s["va"]<=rva and rva+size<=s["va"]+s["size"]]
        need(len(found)==1,"Unmapped or ambiguous PE debug RVA")
        return found[0]
    rva,size=num("<II",opt+112+6*8)
    need(size>0 and size%28==0,"Invalid debug-directory size")
    directory=rva_offset(rva,size); rsds=[]
    for i in range(size//28):
        at=directory+i*28
        typ,sz,addr,ptr=num("<IIII",at+12)
        span(ptr,sz)
        if typ!=2: continue
        need(sz>=25 and span(ptr,4)==b"RSDS","Unsupported CodeView record")
        need(addr==0 or rva_offset(addr,sz)==ptr,"CodeView RVA/file pointer mismatch")
        path=span(ptr+24,sz-24)
        end=path.find(b"\0")
        need(end>=0 and not any(path[end:]),"CodeView path is not NUL-padded")
        need(not any(s["chars"]&0x20000000 and max(ptr,s["ptr"])<min(ptr+sz,s["ptr"]+s["size"]) for s in sections),"CodeView intersects executable section")
        need(not(max(ptr+24,directory)<min(ptr+sz,directory+size)),"CodeView path overlaps debug directory")
        rsds.append((ptr+24,sz-24,ptr))
    need(len(rsds)==1,"Expected exactly one RSDS record")
    return rsds[0],sections


def sanitize_dll(data):
    (start,size,_),_=pe_metadata(data)
    value=b"SuperPeasantNative.pdb\0"
    need(len(value)<=size,"RSDS path field too small")
    return data[:start]+value.ljust(size,b"\0")+data[start+size:]


def verify_dll(before,after):
    (start,size,record),sections=pe_metadata(before)
    parsed_after=pe_metadata(after)
    need(parsed_after==((start,size,record),sections),"PE directory/section structure changed")
    need(len(before)==len(after),"DLL size changed")
    need(before[:start]==after[:start] and before[start+size:]==after[start+size:],"DLL bytes outside RSDS path changed")
    need(after[start:start+size]==b"SuperPeasantNative.pdb\0".ljust(size,b"\0"),"Unexpected RSDS path")
    executable=[]
    for s in sections:
        if s["chars"]&0x20000000 or s["name"]==".text":
            a=before[s["ptr"]:s["ptr"]+s["size"]]; b=after[s["ptr"]:s["ptr"]+s["size"]]
            need(a==b,"Executable section changed")
            executable.append({"name":s["name"],"bytes":s["size"],"sha256":sha(a)})
    need(any(s["name"]==".text" for s in executable),"Missing .text section")
    return {"format":"AMD64 PE32+", "rsds_offset":record,"allowed_path_offset":start,"allowed_path_bytes":size,
            "rsds_guid_age_sha256":sha(before[record:record+24]),"all_other_bytes_sha256":sha(before[:start]+before[start+size:]),
            "executable_sections":executable,"runtime_unchanged":True}


def files(package):
    pex=sorted(package.rglob("*.pex"))
    dll=package/"pkg_Data/F4SE/Plugins/SuperPeasantNative.dll"
    need(len(pex)==9 and dll.is_file(),"Expected final package with nine PEX and one native DLL")
    return pex+[dll]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--package",type=Path,required=True)
    mode=ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check-only",action="store_true")
    mode.add_argument("--out-dir",type=Path)
    mode.add_argument("--verify-dir",type=Path)
    ap.add_argument("--report",type=Path,required=True)
    args=ap.parse_args(); package=args.package.resolve(strict=True)
    need(not args.report.exists(),"Report already exists; choose a new report path")
    destination=(args.out_dir or args.verify_dir)
    if destination:
        destination=destination.resolve()
        need(destination!=package and package not in destination.parents and destination not in package.parents,"Output/input trees overlap")
        if args.out_dir: need(not destination.exists(),"Output directory must not exist")
    need(package not in args.report.resolve().parents,"Do not write reports into input package")
    pending=[]; reports=[]
    for path in files(package):
        relative=path.relative_to(package); before=path.read_bytes()
        transform,verify=(sanitize_pex,verify_pex) if path.suffix.lower()==".pex" else (sanitize_dll,verify_dll)
        after=(destination/relative).read_bytes() if args.verify_dir else transform(before)
        proof=verify(before,after)
        proof.update(file=relative.as_posix(),before_sha256=sha(before),after_sha256=sha(after),before_bytes=len(before),after_bytes=len(after))
        reports.append(proof); pending.append((relative,after))
    # No binary output is created unless every transformation verifies first.
    if args.out_dir:
        destination.mkdir(parents=True,exist_ok=False)
        for relative,data in pending:
            target=destination/relative; target.parent.mkdir(parents=True,exist_ok=True)
            with target.open("xb") as stream: stream.write(data)
            need(target.read_bytes()==data,"Written output failed readback")
    result={"schema":1,"mode":"verify" if args.verify_dir else "check-only" if args.check_only else "sanitize",
            "scope":"Only nine PEX and one native DLL; package layout retained; no other assets copied", "runtime_unchanged":True,"files":reports}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    with args.report.open("x",encoding="utf-8") as stream: json.dump(result,stream,indent=2)
    print(json.dumps({"files":len(reports),"runtime_unchanged":True,"mode":result["mode"]}))


if __name__=="__main__":
    try: main()
    except (Invalid,OSError,UnicodeError,struct.error) as error:
        # Never echo a private source string or absolute filesystem path.
        raise SystemExit("FAIL: "+(str(error) if isinstance(error,Invalid) else type(error).__name__))
