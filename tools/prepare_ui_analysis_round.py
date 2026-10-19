"""Select another bounded set of functions in an existing local image export."""
import argparse
import json
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--name', required=True)
    parser.add_argument('--rvas', required=True, nargs='+', type=lambda value: int(value, 0))
    args = parser.parse_args()
    if not args.name.replace('-', '').replace('_', '').isalnum():
        parser.error('name must contain only letters, digits, hyphens and underscores')
    target = args.manifest.parent / (args.name + '.json')
    if target.exists():
        raise RuntimeError('Analysis round manifest already exists')
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    manifest['named_seeds'] = [x for x in manifest['named_seeds'] if int(x['rva'], 0) in args.rvas]
    manifest['seeds'] = sorted(set(args.rvas))
    manifest['analysis_round'] = args.name
    manifest['source_manifest'] = args.manifest.name
    index = args.manifest.parent / 'decompiled/index.json'
    if index.exists():
        shutil.copy2(index, args.manifest.parent / (args.name + '-previous-index.json'))
    target.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(target)


if __name__ == '__main__':
    main()
