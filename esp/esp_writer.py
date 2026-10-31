"""Writer for Fallout 4 plugins, the mirror of esm_reader.py. Stdlib only.

Serialising
  subrecords_bytes(subs)            -> bytes   (emits XXXX + size-0 header for data > 0xFFFF or when sub.xxxx)
  record_bytes(rec, zlevel=9)       -> bytes   (24-byte header + body; a COMPRESSED record is re-deflated)
  group_bytes(group, zlevel=9)      -> bytes   (recomputes every group size)
  plugin_bytes(plugin, update_hedr=True, next_object_id=None) -> bytes
  write_plugin(plugin, path, ...)   writes the file; refuses partial plugins and any path inside the game folder.

A record whose subrecords were never decoded is written from its stored bytes unchanged. Once .subrecords has been
touched, the body is rebuilt from the subrecord list (and recompressed with zlib level 9 if the record keeps its
COMPRESSED flag; vanilla Fallout4.esm uses level 9, header 78 DA, and Python's zlib reproduces it byte for byte -
see test_roundtrip.py).

Building
  sub(sig, data) / zstr(s) / u8 u16 u32 i32 f32 fid / pack(fmt, *v)
  new_plugin(masters, author="SuperPeasant build", description=None, next_object_id=0x800) -> Plugin
  add_record(plugin, rec)           puts a top-level record into its top group (created in vanilla group order)
  template_copy(rec, formid, edid)  deep copy of a vanilla record: decompressed, new FormID, new EDID, VC info cleared
  set_sub(rec, sig, data, after=None) / remove_subs(rec, sig) / replace_sub(...)
  load_order_formid(master_index, local_id)

Load-order rule: in a plugin whose masters are [Fallout4.esm], a FormID 0x00xxxxxx refers to Fallout4.esm and
0x01xxxxxx is the plugin's own record (master count = 1). Own local IDs start at 0x000800.

Localized master warning: Fallout4.esm has TES4 flag 0x80 (localized). Its FULL/DESC/... subrecords hold 4-byte
string-table IDs, not text. A template copy into a NON-localized .esp must replace those subrecords with real
zero-terminated strings (or drop them). LSTRING_SIGS lists the ones seen in our record types.
"""
import os
import struct
import zlib

import esm_reader as er
from esm_reader import FLAG_COMPRESSED, GRP_HDR, HDR_SIZE, REC_HDR, Group, Plugin, Record, Subrecord

FORM_VERSION = 131          # Fallout 4 1.11 record version (TES4.version and every new record)
HEDR_VERSION = 1.0
DEFAULT_ZLEVEL = 9

# Top-group order of Fallout4.esm (1.11.240), with its duplicate GRAS..BNDS run removed. New plugins use this order.
TOP_ORDER = [
    "GMST", "KYWD", "LCRT", "AACT", "TRNS", "CMPO", "TXST", "GLOB", "DMGT", "CLAS", "FACT", "HDPT", "RACE", "SOUN",
    "ASPC", "MGEF", "LTEX", "ENCH", "SPEL", "ACTI", "TACT", "ARMO", "BOOK", "CONT", "DOOR", "INGR", "LIGH", "MISC",
    "STAT", "SCOL", "MSTT", "GRAS", "TREE", "FLOR", "FURN", "WEAP", "AMMO", "NPC_", "LVLN", "KEYM", "ALCH", "IDLM",
    "NOTE", "PROJ", "HAZD", "BNDS", "TERM", "LVLI", "WTHR", "CLMT", "SPGD", "RFCT", "REGN", "NAVI", "CELL", "WRLD",
    "QUST", "IDLE", "PACK", "CSTY", "LSCR", "ANIO", "WATR", "EFSH", "EXPL", "DEBR", "IMGS", "IMAD", "FLST", "PERK",
    "BPTD", "ADDN", "AVIF", "CAMS", "CPTH", "VTYP", "MATT", "IPCT", "IPDS", "ARMA", "ECZN", "LCTN", "MESG", "DOBJ",
    "DFOB", "LGTM", "MUSC", "FSTP", "FSTS", "SMBN", "SMQN", "SMEN", "MUST", "DLVW", "EQUP", "RELA", "ASTP", "OTFT",
    "ARTO", "MATO", "MOVT", "SNDR", "SNCT", "SOPM", "COLL", "CLFM", "REVB", "PKIN", "RFGP", "AMDL", "LAYR", "COBJ",
    "OMOD", "MSWP", "ZOOM", "INNR", "KSSM", "AECH", "SCCO", "AORU", "SCSN", "STAG", "NOCM", "LENS", "GDRY", "OVIS",
]
# Record types that live inside CELL/WRLD/QUST/DIAL child groups; add_record() refuses them.
NESTED_ONLY = {"REFR", "ACHR", "PGRE", "PMIS", "PHZD", "PARW", "PBAR", "PBEA", "PCON", "PFLA", "NAVM", "LAND",
               "INFO", "DIAL", "DLBR", "SCEN"}

# Localized-string subrecords per record type (Fallout4.esm stores 4-byte string IDs in these).
# From the xEdit FO4 definitions; test_roundtrip.py R7 confirms every occurrence in Fallout4.esm is exactly 4 bytes
# (e.g. NPC_ FULL x1944, QUST NNAM x1198, MESG ITXT x231). ARMO also has FULL inside its object-template block.
# NPC_ ATTX never occurs in Fallout4.esm. PERK EPFD can hold an lstring for some entry-point types (not listed).
LSTRING_SIGS = {
    "NPC_": ["FULL", "SHRT", "ATTX"],
    "QUST": ["FULL", "NNAM"],
    "ARMO": ["FULL", "DESC"],
    "MGEF": ["FULL", "DNAM"],
    "SPEL": ["FULL", "DESC"],
    "ALCH": ["FULL", "DESC"],
    "MESG": ["FULL", "DESC", "ITXT"],
    "PERK": ["FULL", "DESC", "EPF2"],
    "FLST": ["FULL"],
    "EXPL": ["FULL"],
}

_GAME_DIR_MARKERS = (os.sep + "steamapps" + os.sep + "common" + os.sep + "fallout 4",)


# ---------------------------------------------------------------- primitive packers
def u8(v):
    return struct.pack("<B", v)


def u16(v):
    return struct.pack("<H", v)


def i16(v):
    return struct.pack("<h", v)


def u32(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def i32(v):
    return struct.pack("<i", v)


def f32(v):
    return struct.pack("<f", v)


def fid(v):
    return struct.pack("<I", v & 0xFFFFFFFF)


def pack(fmt, *vals):
    return struct.pack("<" + fmt, *vals)


def zstr(s):
    """Zero-terminated cp1252 string (the CK encoding for non-localized plugins)."""
    return s.encode("cp1252") + b"\0"


def wstr(s):
    """u16 length-prefixed string without terminator (used inside VMAD)."""
    b = s.encode("cp1252")
    return struct.pack("<H", len(b)) + b


def sub(sig, data):
    return Subrecord(sig, data)


# ---------------------------------------------------------------- serialising
def subrecords_bytes(subs):
    parts = []
    for s in subs:
        sig = s.sig.encode("latin-1")
        n = len(s.data)
        if n > 0xFFFF or s.xxxx:
            parts.append(b"XXXX\x04\x00" + struct.pack("<I", n))
            parts.append(sig + b"\0\0")
        else:
            parts.append(sig + struct.pack("<H", n))
        parts.append(s.data)
    return b"".join(parts)


def compress_body(data, zlevel=DEFAULT_ZLEVEL):
    return struct.pack("<I", len(data)) + zlib.compress(data, zlevel)


def record_body(rec, zlevel=DEFAULT_ZLEVEL):
    """The data section of a record as it goes on disk."""
    if rec._subs is None and rec._raw is not None:
        return rec._raw  # never decoded, so never edited
    data = subrecords_bytes(rec._subs)
    if rec.flags & FLAG_COMPRESSED:
        return compress_body(data, zlevel)
    return data


def record_bytes(rec, zlevel=DEFAULT_ZLEVEL):
    body = record_body(rec, zlevel)
    return REC_HDR.pack(rec.sig.encode("latin-1"), len(body), rec.flags, rec.formid & 0xFFFFFFFF,
                        rec.vc1, rec.vc2, rec.version, rec.unknown) + body


def group_bytes(g, zlevel=DEFAULT_ZLEVEL):
    parts = []
    for c in g.children:
        if isinstance(c, Group):
            parts.append(group_bytes(c, zlevel))
        else:
            parts.append(record_bytes(c, zlevel))
    body = b"".join(parts)
    return GRP_HDR.pack(b"GRUP", HDR_SIZE + len(body), g.label, g.gtype, g.vc1, g.vc2, g.unknown) + body


def count_records_and_groups(groups):
    n = 0
    for g in groups:
        n += 1
        for c in g.children:
            if isinstance(c, Group):
                n += count_records_and_groups([c])
            else:
                n += 1
    return n


def plugin_bytes(plugin, update_hedr=True, next_object_id=None, zlevel=DEFAULT_ZLEVEL):
    if plugin.partial:
        raise ValueError("refusing to serialise a partially loaded plugin (Plugin.load(..., sigs=...))")
    hdr = plugin.header
    if update_hedr:
        h = hdr.get("HEDR")
        ver, _, nid = struct.unpack("<fiI", h.data)
        if next_object_id is not None:
            nid = next_object_id
        h.data = struct.pack("<fiI", ver, count_records_and_groups(plugin.groups), nid)
    return record_bytes(hdr, zlevel) + b"".join(group_bytes(g, zlevel) for g in plugin.groups)


def _inside_game_folder(path):
    p = os.path.normcase(os.path.abspath(path))
    return any(m in p for m in _GAME_DIR_MARKERS)


def write_plugin(plugin, path, update_hedr=True, next_object_id=None, allow_game_folder=False):
    """Write a plugin file. Never writes into the game folder unless allow_game_folder=True (install.ps1's job)."""
    if not allow_game_folder and _inside_game_folder(path):
        raise PermissionError("esp_writer refuses to write inside the game folder: %s" % path)
    data = plugin_bytes(plugin, update_hedr, next_object_id)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    return len(data)


# ---------------------------------------------------------------- building
def new_plugin(masters=("Fallout4.esm",), author="SuperPeasant build", description=None,
               next_object_id=0x800, esm=False, esl=False):
    subs = [Subrecord("HEDR", struct.pack("<fiI", HEDR_VERSION, 0, next_object_id)),
            Subrecord("CNAM", zstr(author))]
    if description:
        subs.append(Subrecord("SNAM", zstr(description)))
    for m in masters:
        subs.append(Subrecord("MAST", zstr(m)))
        subs.append(Subrecord("DATA", b"\0" * 8))
    subs.append(Subrecord("INTV", struct.pack("<I", 1)))
    flags = (er.FLAG_ESM if esm else 0) | (er.FLAG_ESL if esl else 0)
    hdr = Record("TES4", 0, flags, subs, version=FORM_VERSION)
    return Plugin(hdr, [], None, partial=False)


def load_order_formid(master_index, local_id):
    return ((master_index & 0xFF) << 24) | (local_id & 0x00FFFFFF)


def add_record(plugin, rec):
    """Append a top-level record to the plugin's top group for its type (creating the group in TOP_ORDER)."""
    if rec.sig in NESTED_ONLY:
        raise ValueError("%s records live in child groups; add_record only handles top-level types" % rec.sig)
    for g in plugin.groups:
        if g.gtype == 0 and g.label_sig == rec.sig:
            g.children.append(rec)
            return rec
    g = Group(rec.sig, 0, [rec])
    rank = TOP_ORDER.index(rec.sig) if rec.sig in TOP_ORDER else len(TOP_ORDER)
    i = 0
    while i < len(plugin.groups):
        og = plugin.groups[i]
        orank = TOP_ORDER.index(og.label_sig) if og.label_sig in TOP_ORDER else len(TOP_ORDER)
        if orank > rank:
            break
        i += 1
    plugin.groups.insert(i, g)
    return rec


def new_record(sig, formid, subs, flags=0):
    return Record(sig, formid, flags, list(subs), version=FORM_VERSION)


def template_copy(rec, formid, edid=None, keep_flags_mask=~FLAG_COMPRESSED):
    """Copy a vanilla record as the start of a new one: decompressed, new FormID/EDID, version-control info cleared."""
    r = rec.copy(keep_raw=False)
    r.formid = formid
    r.flags = rec.flags & keep_flags_mask
    r.vc1 = r.vc2 = 0
    r.version = FORM_VERSION
    r.unknown = 0
    if edid is not None:
        set_sub(r, "EDID", zstr(edid), first=True)
    return r


def set_sub(rec, sig, data, after=None, first=False):
    """Replace the first subrecord `sig` (keeping its position), or insert it (at the front, after the first `after`
    subrecord, or at the end)."""
    subs = rec.subrecords
    for s in subs:
        if s.sig == sig:
            s.data = bytes(data)
            s.xxxx = False
            return s
    s = Subrecord(sig, data)
    if first:
        subs.insert(0, s)
    elif after is not None and rec.index(after) >= 0:
        subs.insert(rec.index(after) + 1, s)
    else:
        subs.append(s)
    return s


def remove_subs(rec, *sigs):
    before = len(rec.subrecords)
    rec.subrecords = [s for s in rec.subrecords if s.sig not in sigs]
    return before - len(rec.subrecords)


def replace_sub(rec, index, sig, data):
    rec.subrecords[index] = Subrecord(sig, data)
