"""Read-only parser for Fallout 4 plugin files (.esm / .esp / .esl), stdlib only.

Format (Fallout 4, form version 131, checked on Fallout4.esm 1.11.240):

  record header, 24 bytes:  sig[4] dataSize:u32 flags:u32 formID:u32 vc1:u16 vc2:u16 version:u16 unknown:u16
  group  header, 24 bytes:  'GRUP' groupSize:u32 (incl. header) label[4] groupType:i32 vc1:u16 vc2:u16 unknown:u32
  subrecord:                sig[4] size:u16 data[size]
  large subrecord:          'XXXX' 4:u16 realSize:u32, then sig[4] size:u16 (normally 0) data[realSize]
  compressed record:        flags & 0x00040000; data = decompressedSize:u32 + zlib stream (vanilla: 78 DA, level 9)

Fallout4.esm quirks this reader handles: top groups GRAS..BNDS appear TWICE (two separate top groups with the
same label), there is an empty TREE group (24 bytes), and HEDR's record count is stale (1,741,853 vs
1,549,276 records + 112,381 groups). The DLC masters' HEDR count = records (without TES4) + groups.

Main API
  Plugin.load(path, sigs=None)    -> Plugin (header Record + list of top Groups). With `sigs`, only top groups whose
                                     label is in `sigs` are parsed (the result is marked partial and cannot be saved).
  iter_records(path, sigs=None, top=None, with_path=False) -> yields every Record (any depth) whose sig is in sigs
                                     (with_path: (Record, [enclosing Groups]) so REFRs know their cell/world).
  get_records(path, formids, sigs=None) -> {formid: Record}, e.g. the vanilla templates build_esp copies.
  Record: .sig .formid .flags .vc1 .vc2 .version .unknown, .subrecords (list of Subrecord, decoded lazily),
          .edid, .get(sig), .get_all(sig), .data_bytes() (decompressed subrecord stream), .is_compressed
  Subrecord: .sig (str) .data (bytes) .xxxx (True when it was stored behind an XXXX size marker)

Run Python that reads game files with -I. This module never writes anything.
"""
import mmap
import struct
import zlib

FLAG_ESM = 0x00000001
FLAG_DELETED = 0x00000020
FLAG_LOCALIZED = 0x00000080  # on TES4: lstrings are 4-byte string-table IDs
FLAG_ESL = 0x00000200  # on TES4
FLAG_COMPRESSED = 0x00040000

HDR_SIZE = 24
REC_HDR = struct.Struct("<4sIIIHHHH")   # sig, dataSize, flags, formid, vc1, vc2, version, unknown
GRP_HDR = struct.Struct("<4sI4siHHI")   # 'GRUP', groupSize, label, groupType, vc1, vc2, unknown
SUB_HDR = struct.Struct("<4sH")
_U16 = struct.Struct("<H")
_U32 = struct.Struct("<I")

GROUP_TYPES = {
    0: "Top",
    1: "World Children",
    2: "Interior Cell Block",
    3: "Interior Cell Sub-Block",
    4: "Exterior Cell Block",
    5: "Exterior Cell Sub-Block",
    6: "Cell Children",
    7: "Topic Children",
    8: "Cell Persistent Children",
    9: "Cell Temporary Children",
    10: "Quest Children",
}
# group types whose label is a parent FormID
FORMID_LABEL_GROUPS = (1, 6, 7, 8, 9, 10)


class ESPFormatError(ValueError):
    pass


class Subrecord(object):
    __slots__ = ("sig", "data", "xxxx")

    def __init__(self, sig, data, xxxx=False):
        if isinstance(sig, bytes):
            sig = sig.decode("latin-1")
        if len(sig) != 4:
            raise ESPFormatError("subrecord signature must be 4 chars: %r" % (sig,))
        self.sig = sig
        self.data = bytes(data)
        self.xxxx = xxxx

    def __repr__(self):
        return "Subrecord(%s, %d bytes%s)" % (self.sig, len(self.data), ", XXXX" if self.xxxx else "")

    def __eq__(self, other):
        return isinstance(other, Subrecord) and self.sig == other.sig and self.data == other.data

    def __ne__(self, other):
        return not self.__eq__(other)

    def copy(self):
        return Subrecord(self.sig, self.data, self.xxxx)

    # small typed readers
    def u8(self, off=0):
        return self.data[off]

    def u16(self, off=0):
        return struct.unpack_from("<H", self.data, off)[0]

    def u32(self, off=0):
        return struct.unpack_from("<I", self.data, off)[0]

    def i32(self, off=0):
        return struct.unpack_from("<i", self.data, off)[0]

    def f32(self, off=0):
        return struct.unpack_from("<f", self.data, off)[0]

    def zstring(self):
        """Null-terminated string (cp1252, as the CK writes it)."""
        d = self.data
        i = d.find(b"\0")
        if i >= 0:
            d = d[:i]
        return d.decode("cp1252", errors="replace")


def parse_subrecords(data):
    """Split a decompressed record body into Subrecords. Raises ESPFormatError on overrun."""
    out = []
    i = 0
    n = len(data)
    big = None
    unpack = SUB_HDR.unpack_from
    while i < n:
        if i + 6 > n:
            raise ESPFormatError("truncated subrecord header at %d of %d" % (i, n))
        sig, size = unpack(data, i)
        i += 6
        if sig == b"XXXX":
            if size != 4:
                raise ESPFormatError("XXXX with size %d" % size)
            big = _U32.unpack_from(data, i)[0]
            i += 4
            continue
        if big is not None:
            if size != 0:
                raise ESPFormatError("subrecord after XXXX has nonzero size field %d" % size)
            size = big
            sub = Subrecord(sig, data[i:i + size], True)
            big = None
        else:
            sub = Subrecord(sig, data[i:i + size], False)
        if i + size > n:
            raise ESPFormatError("subrecord %r overruns record (%d + %d > %d)" % (sig, i, size, n))
        out.append(sub)
        i += size
    if big is not None:
        raise ESPFormatError("dangling XXXX at end of record")
    return out


class Record(object):
    """One record. The body is decoded into subrecords on first access to .subrecords."""

    __slots__ = ("sig", "flags", "formid", "vc1", "vc2", "version", "unknown", "_raw", "_subs", "offset")

    def __init__(self, sig, formid=0, flags=0, subrecords=None, vc1=0, vc2=0, version=131, unknown=0,
                 raw=None, offset=None):
        if isinstance(sig, bytes):
            sig = sig.decode("latin-1")
        self.sig = sig
        self.formid = formid
        self.flags = flags
        self.vc1 = vc1
        self.vc2 = vc2
        self.version = version
        self.unknown = unknown
        self._raw = raw          # data bytes exactly as stored (compressed form if flags has COMPRESSED)
        self._subs = subrecords  # list of Subrecord, or None until decoded
        self.offset = offset     # file offset of the record header, when read from a file
        if raw is None and subrecords is None:
            self._subs = []

    # ---- body access
    @property
    def is_compressed(self):
        return bool(self.flags & FLAG_COMPRESSED)

    @property
    def is_deleted(self):
        return bool(self.flags & FLAG_DELETED)

    def stored_bytes(self):
        """The data section exactly as read from the file (None for records built in memory)."""
        return self._raw

    def data_bytes(self):
        """The decompressed subrecord stream as stored (only valid before edits; see esp_writer for re-serialising)."""
        if self._raw is None:
            raise ValueError("record was built in memory; use esp_writer.subrecords_bytes()")
        if self.flags & FLAG_COMPRESSED:
            return decompress_body(self._raw)
        return self._raw

    @property
    def subrecords(self):
        if self._subs is None:
            self._subs = parse_subrecords(self.data_bytes())
        return self._subs

    @subrecords.setter
    def subrecords(self, subs):
        self._subs = list(subs)

    # ---- convenience
    def get(self, sig, default=None):
        for s in self.subrecords:
            if s.sig == sig:
                return s
        return default

    def get_all(self, sig):
        return [s for s in self.subrecords if s.sig == sig]

    def index(self, sig, start=0):
        subs = self.subrecords
        for i in range(start, len(subs)):
            if subs[i].sig == sig:
                return i
        return -1

    @property
    def edid(self):
        s = self.get("EDID")
        return s.zstring() if s is not None else None

    @property
    def sig_list(self):
        return [s.sig for s in self.subrecords]

    def copy(self, keep_raw=False):
        """Deep copy. keep_raw=False detaches the copy from the stored bytes (decodes subrecords first)."""
        r = Record(self.sig, self.formid, self.flags, None, self.vc1, self.vc2, self.version, self.unknown)
        if keep_raw and self._subs is None:
            r._raw = self._raw
            r._subs = None
        else:
            r._subs = [s.copy() for s in self.subrecords]
            r._raw = None
        return r

    def __repr__(self):
        ed = None
        try:
            ed = self.edid
        except ESPFormatError:
            pass
        return "Record(%s %08X %s flags=%08X)" % (self.sig, self.formid, ed, self.flags)


class Group(object):
    __slots__ = ("label", "gtype", "vc1", "vc2", "unknown", "children", "offset")

    def __init__(self, label, gtype=0, children=None, vc1=0, vc2=0, unknown=0, offset=None):
        if isinstance(label, str):
            label = label.encode("latin-1")
        elif isinstance(label, int):
            label = struct.pack("<I", label & 0xFFFFFFFF)
        if len(label) != 4:
            raise ESPFormatError("group label must be 4 bytes")
        self.label = bytes(label)
        self.gtype = gtype
        self.vc1 = vc1
        self.vc2 = vc2
        self.unknown = unknown
        self.children = children if children is not None else []
        self.offset = offset

    @property
    def label_sig(self):
        return self.label.decode("latin-1")

    @property
    def label_formid(self):
        return _U32.unpack(self.label)[0]

    def iter_records(self, sigs=None):
        """Every record below this group, depth first, in file order."""
        for c in self.children:
            if isinstance(c, Group):
                for r in c.iter_records(sigs):
                    yield r
            elif sigs is None or c.sig in sigs:
                yield c

    def __repr__(self):
        if self.gtype == 0:
            lab = self.label_sig
        elif self.gtype in FORMID_LABEL_GROUPS:
            lab = "%08X" % self.label_formid
        else:
            lab = self.label.hex()
        return "Group(%s %s, %d children)" % (GROUP_TYPES.get(self.gtype, self.gtype), lab, len(self.children))


def decompress_body(raw):
    if len(raw) < 4:
        raise ESPFormatError("compressed record shorter than 4 bytes")
    want = _U32.unpack_from(raw, 0)[0]
    out = zlib.decompress(bytes(raw[4:]))
    if len(out) != want:
        raise ESPFormatError("decompressed size %d != declared %d" % (len(out), want))
    return out


def read_record_at(buf, pos):
    """Parse the record whose header starts at buf[pos]. Returns (Record, end_pos)."""
    sig, dsize, flags, fid, vc1, vc2, ver, unk = REC_HDR.unpack_from(buf, pos)
    if sig == b"GRUP":
        raise ESPFormatError("expected a record at %d, found GRUP" % pos)
    end = pos + HDR_SIZE + dsize
    if end > len(buf):
        raise ESPFormatError("record %r %08X at %d overruns the file" % (sig, fid, pos))
    raw = bytes(buf[pos + HDR_SIZE:end])
    return Record(sig, fid, flags, None, vc1, vc2, ver, unk, raw=raw, offset=pos), end


def read_group_at(buf, pos, sigs_filter=None):
    """Parse the group whose header starts at buf[pos] (recursively). Returns (Group, end_pos)."""
    tag, gsize, label, gtype, vc1, vc2, unk = GRP_HDR.unpack_from(buf, pos)
    if tag != b"GRUP":
        raise ESPFormatError("expected GRUP at %d, found %r" % (pos, tag))
    end = pos + gsize
    if gsize < HDR_SIZE or end > len(buf):
        raise ESPFormatError("bad group size %d at %d" % (gsize, pos))
    g = Group(label, gtype, [], vc1, vc2, unk, offset=pos)
    p = pos + HDR_SIZE
    while p < end:
        if buf[p:p + 4] == b"GRUP":
            child, p = read_group_at(buf, p, sigs_filter)
        else:
            child, p = read_record_at(buf, p)
        g.children.append(child)
    if p != end:
        raise ESPFormatError("group at %d: children end at %d, header says %d" % (pos, p, end))
    return g, end


class Plugin(object):
    """A whole plugin: TES4 header record + top-level groups (in file order, duplicates kept)."""

    def __init__(self, header=None, groups=None, path=None, partial=False):
        self.header = header
        self.groups = groups if groups is not None else []
        self.path = path
        self.partial = partial

    @classmethod
    def load(cls, path, sigs=None):
        with open(path, "rb") as f:
            mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
            try:
                return cls.from_buffer(mm, sigs, path)
            finally:
                mm.close()

    @classmethod
    def from_bytes(cls, data, sigs=None):
        return cls.from_buffer(data, sigs, None)

    @classmethod
    def from_buffer(cls, buf, sigs=None, path=None):
        header, pos = read_record_at(buf, 0)
        if header.sig != "TES4":
            raise ESPFormatError("not a plugin: first record is %r" % header.sig)
        n = len(buf)
        groups = []
        want = set(sigs) if sigs else None
        while pos < n:
            tag, gsize, label = struct.unpack_from("<4sI4s", buf, pos)
            if tag != b"GRUP":
                raise ESPFormatError("expected top-level GRUP at %d" % pos)
            if want is not None and label.decode("latin-1") not in want:
                pos += gsize
                continue
            g, pos = read_group_at(buf, pos)
            groups.append(g)
        return cls(header, groups, path, partial=want is not None)

    # ---- header helpers
    @property
    def masters(self):
        return [s.zstring() for s in self.header.subrecords if s.sig == "MAST"]

    @property
    def is_localized(self):
        return bool(self.header.flags & FLAG_LOCALIZED)

    @property
    def hedr(self):
        """(version float, numRecords int, nextObjectID int)"""
        return struct.unpack("<fiI", self.header.get("HEDR").data)

    def top_groups(self, sig):
        return [g for g in self.groups if g.gtype == 0 and g.label_sig == sig]

    def iter_records(self, sigs=None):
        for g in self.groups:
            for r in g.iter_records(sigs):
                yield r

    def find(self, formid=None, edid=None, sig=None):
        for r in self.iter_records({sig} if sig else None):
            if formid is not None and r.formid != formid:
                continue
            if edid is not None and r.edid != edid:
                continue
            return r
        return None


def iter_records(path, sigs=None, top=None, with_path=False):
    """Stream records from a plugin without building the tree.

    sigs: set of record signatures to yield (None = all). top: set of top-group labels to enter (None = all;
    pass e.g. {"CELL", "WRLD"} to only walk cells). Yields Record (or (Record, [group stack]) with with_path).
    Each yielded Record owns a copy of its bytes, so it stays valid after the file is closed.
    """
    with open(path, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            n = len(mm)
            hdr, pos = read_record_at(mm, 0)
            stack = []  # (end, Group-without-children)
            while pos < n:
                while stack and pos >= stack[-1][0]:
                    stack.pop()
                if mm[pos:pos + 4] == b"GRUP":
                    tag, gsize, label, gtype, vc1, vc2, unk = GRP_HDR.unpack_from(mm, pos)
                    if not stack and top is not None and label.decode("latin-1") not in top:
                        pos += gsize
                        continue
                    stack.append((pos + gsize, Group(label, gtype, None, vc1, vc2, unk, offset=pos)))
                    pos += HDR_SIZE
                    continue
                sig = mm[pos:pos + 4].decode("latin-1")
                dsize = _U32.unpack_from(mm, pos + 4)[0]
                if sigs is None or sig in sigs:
                    rec, _ = read_record_at(mm, pos)
                    if with_path:
                        yield rec, [g for _, g in stack]
                    else:
                        yield rec
                pos += HDR_SIZE + dsize
        finally:
            mm.close()


def get_records(path, formids, sigs=None):
    """{formid: Record} for the given FormIDs (as stored in that file). With `sigs`, only those top groups are
    walked (fast: about 2 s for Fallout4.esm without CELL/WRLD). Missing FormIDs are simply absent."""
    want = set(formids)
    out = {}
    top = set(sigs) if sigs else None
    for rec in iter_records(path, sigs=set(sigs) if sigs else None, top=top):
        if rec.formid in want:
            out[rec.formid] = rec
            if len(out) == len(want):
                break
    return out


def read_header(path):
    """Just the TES4 record of a plugin."""
    with open(path, "rb") as f:
        head = f.read(HDR_SIZE)
        dsize = _U32.unpack_from(head, 4)[0]
        rec, _ = read_record_at(head + f.read(dsize), 0)
        return rec
