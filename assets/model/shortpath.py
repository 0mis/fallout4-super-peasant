"""8.3 path for the nifly DLL, which cannot open paths longer than MAX_PATH (260 chars).

Python in Blender sees this workspace through the Claude package's LocalCache folder (a long path); the same folder is
reachable as %APPDATA%\\Claude\\..., whose components have 8.3 short names. A path that does not exist yet (an export
target) keeps its file name and gets the short form of its folder.
"""
import os, re, ctypes

_PKG = re.compile(r"^[A-Za-z]:\\Users\\([^\\]+)\\AppData\\Local\\Packages\\Claude_[^\\]+\\LocalCache\\Roaming\\", re.I)


def short(p):
    p = os.path.abspath(p)
    if os.name != "nt":
        return p
    p = _PKG.sub(lambda m: "C:\\Users\\" + m.group(1) + "\\AppData\\Roaming\\", p)
    head, tail = (p, "") if os.path.exists(p) else os.path.split(p)
    buf = ctypes.create_unicode_buffer(2048)
    n = ctypes.windll.kernel32.GetShortPathNameW(head, buf, 2048)
    s = buf.value if 0 < n < 2048 else head
    return os.path.join(s, tail) if tail else s


if __name__ == "__main__":
    import sys
    for a in sys.argv[1:]:
        print(short(a), len(short(a)))
