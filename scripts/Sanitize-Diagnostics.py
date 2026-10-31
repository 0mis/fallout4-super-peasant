"""Opt-in diagnostic-literal extension. Pure functions; no file writes or CLI.
Accepts only the exact already-RSDS-sanitized final DLL. Existing sanitizer unchanged.
Source-location argument evidence is recorded separately; diagnostic text changes.
"""
from pathlib import Path
import hashlib
import runpy

INPUT_SHA = "05eebbe76763d558198aa4d6262855e1d2205251733b2426f9eb7d7b5c4937fc"
FIELDS = ((3060656, 94, '3b37b33057469490eb4bd8c620421001f8180975a1b2c7579c1ec327dabe3ec7', 'BSInputEventUser.h'), (3078872, 47, 'f9b35ea68efb50947c94415ba752567d113b70777ab96eb6898965ec4d289d6d', 'hooks_carry.inc'), (3083712, 103, '8ca7b546e50be32de65806694bad931416ce9d3fdee0602532f0fb0982f43c20', 'THook.h'), (3106736, 86, '0521103d2adb3dbaf40ef9429586992d852c3fba58730bb119c459f4c763b64f', 'BSTArray.h'), (3130528, 84, 'd05521190a1fb2ba977ce63033db51486f1bdc32463bba25fcc13ee8075d19f7', 'NiNode.h'), (3136672, 105, '18a8af017fe550551b7bac936cda2aea10f31ce54737770d6024512f4828e05a', 'BSScript_Internal_IFunction.h'), (3136880, 86, '5993cf9f09a3f4114c1972c5137654c6b1861589553c2b3fd0dc9b8dd79545ae', 'BSTTuple.h'), (3141376, 90, '68e6c0429dce3cf92afd0c7f9212d9ba2ca36062bafd2ddc2a9500923e539da5', 'BSScriptUtil.h'), (3203680, 45, 'f782df9eb8d6242c714d5def893db5d72c32320194e9070877bdd463519eaf89', 'runtime_carry.inc'), (3212328, 48, 'ddc7669f375d0b5079864e3207283ca3a24bd6a3ef4c6b13263940ee4afde406', 'runtime_bookmark.inc'), (3214160, 42, 'bbba476fc607ae4488d10062b6ada8e935b8875bfdddb06dd09e5b11d4eab39c', 'runtime_r6.inc'), (3216096, 46, '91fd55be4bb49a425773a3026520924552abda5e8f86bd512f079294a7dcef58', 'runtime_unstop.inc'), (3217184, 45, '9091089e7881ddcdab2c0758d02e9547f5ad0397e6d2dec1ce762db379b81bfe', 'runtime_shove.inc'), (3217792, 44, '97a3a3d7122f93f57830593e6eee641a546f01dc26d6c1236604ba32896fbee0', 'runtime_cage.inc'), (3220584, 45, 'c81b15203580341725ba20aa7f3b60cdb8e19fffb340aff2dd3ace2e40247383', 'runtime_route.inc'), (3223480, 44, 'a925a1a49460c28438a751c3ffe10ef623cc0efbf2f0b452f1b44ff3053d1652', 'runtime_tele.inc'), (3226280, 46, '3fd3e6ebcceed06a1bb6270720a39bfe4dfcba16b2a20f99212fff394f46efda', 'runtime_follow.inc'), (3228096, 46, 'a9bdccf96c0b8ff8292ce2770b957f6db00ebb44eb178c07fa324844e21835db', 'runtime_moverb.inc'), (3247872, 88, '53e187c82c81703ef80df28a01d170bd405b4ae68835f12fea8131a50d997baf', 'Interfaces.h'))
BASE = runpy.run_path(str(Path(__file__).with_name("Sanitize-Metadata.py")), run_name="metadata_helpers")
need, sha = BASE["need"], BASE["sha"]

def check_input(data):
    need(sha(data) == INPUT_SHA, "Unknown DLL; diagnostic edit refused")
    _, sections = BASE["pe_metadata"](data)
    need(len(FIELDS) == 19, "Invalid whitelist")
    prior = 0
    for offset, size, digest, name in FIELDS:
        need(offset >= prior, "Overlapping diagnostic fields")
        prior = offset + size
        need(sha(data[offset:offset+size]) == digest, "Diagnostic field hash mismatch")
        need(any(s["name"] == ".rdata" and s["ptr"] <= offset and offset+size <= s["ptr"]+s["size"] and not s["chars"] & 0x20000000 for s in sections), "Diagnostic field outside non-executable .rdata")
        need(data[offset+size-1] == 0 and b"\0" not in data[offset:offset+size-1], "Invalid source filename field")
        need(data[offset:offset+size-1].replace(b"\\",b"/").rsplit(b"/",1)[-1] == name.encode("ascii"), "Diagnostic basename mismatch")
    return sections

def verify_diagnostic_literals(before, after):
    sections = check_input(before)
    need(len(after) == len(before), "DLL size changed")
    cursor = 0
    unchanged = bytearray()
    for offset, size, digest, name in FIELDS:
        need(before[cursor:offset] == after[cursor:offset], "Byte outside diagnostic whitelist changed")
        unchanged.extend(before[cursor:offset])
        need(after[offset:offset+size] == (name.encode("ascii")+b"\0").ljust(size,b"\0"), "Unexpected diagnostic replacement")
        cursor = offset+size
    need(before[cursor:] == after[cursor:], "Trailing unapproved byte change")
    unchanged.extend(before[cursor:])
    executable = []
    for s in sections:
        if s["chars"] & 0x20000000:
            a=before[s["ptr"]:s["ptr"]+s["size"]]
            need(a == after[s["ptr"]:s["ptr"]+s["size"]], "Executable bytes changed")
            executable.append({"section":s["name"],"sha256":sha(a)})
    return {"input_sha256":sha(before),"output_sha256":sha(after),"fields":19,
            "all_non_whitelisted_bytes_sha256":sha(unchanged),"executable_sections":executable,
            "scope":"Only exact source_location diagnostic filename literals; all addresses, instructions and other bytes unchanged"}

def patch_diagnostic_literals(data):
    check_input(data)
    out=bytearray(data)
    for offset,size,digest,name in FIELDS:
        replacement=name.encode("ascii")+b"\0"
        need(len(replacement)<=size,"Replacement does not fit")
        out[offset:offset+size]=replacement.ljust(size,b"\0")
    result=bytes(out)
    return result, verify_diagnostic_literals(data,result)
