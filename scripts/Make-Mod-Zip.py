"""Assemble the player Data-root ZIP from source assets and explicit built inputs.

Python stdlib only. This never installs, launches, downloads or edits game files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--scripts', required=True, type=Path, help='Compiled Scripts directory containing the9 canonical PEXs')
    parser.add_argument('--esp', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    template = json.loads((root/'docs/RELEASE_FILES.json').read_text(encoding='utf-8'))
    payload = {}
    for row in template['files']:
        rel = row['path']
        if rel == 'F4SE/Plugins/SuperPeasantNative.dll':
            source = args.dll
        elif rel == 'F4SE/Plugins/SuperPeasantNative.ini':
            source = root/'native/SuperPeasantNative.ini'
        elif rel == 'SuperPeasant.esp':
            source = args.esp
        elif rel.startswith('Scripts/'):
            source = args.scripts/rel.split('/', 1)[1]
        else:
            source = root/'assets/Data'/rel
        if not source.is_file():
            raise SystemExit('Missing required input: '+rel)
        if '/Test/' in rel or 'TestHelper' in rel or rel.endswith('.pdb'):
            raise SystemExit('Test/debug input refused')
        payload[rel] = source.read_bytes()
    if len(payload) != 45:
        raise SystemExit('Expected45 runtime files')
    manifest = dict(template)
    manifest['files'] = [{'path': p, 'bytes': len(b), 'sha256': hashlib.sha256(b).hexdigest()} for p,b in sorted(payload.items())]
    manifest['bytes'] = sum(len(b) for b in payload.values())
    # Rebuilt inputs need their own validation; do not inherit a runtime-proof claim.
    manifest.pop('executable_instructions_unchanged', None)
    manifest.pop('public_metadata_sanitized', None)
    payload['FILES.sha256.json'] = (json.dumps(manifest, indent=2)+'\n').encode('utf-8')
    for rel in ('LICENSE','EXCEPTIONS','THIRD_PARTY.md','ASSET_LICENSE.md'):
        payload[rel] = (root/rel).read_bytes()
    payload['README-PLAYERS.md'] = (root/'docs/PLAYERS.md').read_bytes()
    payload['MANUAL-FILE-LIST.txt'] = ('Runtime files relative to the game Data directory. Preserve saves and shared dependencies.\n\n'+'\n'.join(sorted(x['path'] for x in manifest['files']))+'\n').encode('utf-8')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.out, 'x', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for rel, data in sorted(payload.items()):
            info = zipfile.ZipInfo(rel, (2026,10,31,0,0,0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    print(json.dumps({'archive': str(args.out), 'runtime_files':45, 'bytes': args.out.stat().st_size}))


if __name__ == '__main__':
    main()
