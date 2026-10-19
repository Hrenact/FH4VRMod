"""Run local Ghidra headless on an exported image, without opening the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--ghidra', type=Path)
    parser.add_argument('--java-home', type=Path)
    parser.add_argument('--max-functions', type=int, default=140)
    parser.add_argument('--depth', type=int, default=1)
    parser.add_argument('--reuse', action='store_true')
    args = parser.parse_args()
    if args.max_functions < 1 or args.depth < 0:
        parser.error('max-functions must be positive and depth nonnegative')
    root = Path(__file__).resolve().parents[1]
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    raw = manifest_path.parent / manifest['raw_file']
    with raw.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    if digest != manifest['sha256'] or raw.stat().st_size != manifest['image_size']:
        raise RuntimeError('Export size/hash differs from manifest')
    candidates = sorted((root / '.tools/ghidra').glob('ghidra_*_PUBLIC/support/analyzeHeadless.bat'))
    headless = args.ghidra / 'support/analyzeHeadless.bat' if args.ghidra else (candidates[-1] if candidates else None)
    if headless is None or not headless.is_file():
        raise RuntimeError('Ghidra not installed; use --ghidra to select its portable directory')
    project_dir = manifest_path.parent / 'ghidra-project'
    project_dir.mkdir(exist_ok=True)
    project_name = 'fh4_ui'
    if (project_dir / (project_name + '.gpr')).exists() and not args.reuse:
        raise RuntimeError('Project already exists; use --reuse to continue offline analysis')
    output = manifest_path.parent / 'decompiled'
    env = os.environ.copy()
    if args.java_home:
        env['JAVA_HOME'] = str(args.java_home.resolve())
    command = [str(headless), str(project_dir), project_name]
    if args.reuse:
        command += ['-process', raw.name]
    else:
        command += ['-import', str(raw), '-loader', 'BinaryLoader', '-processor', 'x86:LE:64:default',
                    '-cspec', 'windows', '-loader-baseAddr', hex(manifest['runtime_base'])]
    command += ['-noanalysis', '-scriptPath', str(root / 'tools/ghidra'),
                '-postScript', 'DecompileUi.java', str(manifest_path), str(output),
                str(args.max_functions), str(args.depth), '-max-cpu', '2',
                '-log', str(manifest_path.parent / 'ghidra.log')]
    print('Targeted offline decompilation:', raw, flush=True)
    subprocess.run(command, env=env, check=True)
    index = json.loads((output / 'index.json').read_text(encoding='utf-8'))
    if index['raw_sha256'] != manifest['sha256']:
        raise RuntimeError('Decompiler index is from another export')
    successes = sum(row.get('status') == 'decompiled; requires validation' for row in index['functions'])
    print(f'Decompiled {successes}/{len(index["functions"])} function records; inspect index.json for omissions.')
    if not successes:
        raise RuntimeError('No successful decompilation records')


if __name__ == '__main__':
    main()
