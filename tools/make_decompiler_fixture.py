"""Create a tiny synthetic x64 image to validate the offline decompiler pipeline."""
import hashlib
import json
from pathlib import Path


def make_fixture():
    output = Path(__file__).resolve().parents[1] / 'reports/decompiler-fixture'
    output.mkdir(parents=True, exist_ok=True)
    image = bytearray(0x3000)
    # Win64 caller has shadow space, a real direct call, and a known result (49).
    code = bytes.fromhex('48 83 ec 28 e8 f7 00 00 00 83 c0 07 48 83 c4 28 c3')
    image[0x1000:0x1000 + len(code)] = code
    image[0x1100:0x1106] = bytes.fromhex('b8 2a 00 00 00 c3')
    # A second function has a noncontiguous branch body beyond its first unwind range.
    image[0x1200:0x1209] = bytes.fromhex('85 c9 0f 84 f8 01 00 00 c3')
    image[0x1400:0x1406] = bytes.fromhex('b8 63 00 00 00 c3')
    raw = output / 'fh4-image.bin'
    raw.write_bytes(image)
    manifest = dict(runtime_base=0x180000000, image_size=len(image), raw_file=raw.name,
        sha256=hashlib.sha256(image).hexdigest(), unreadable=[],
        sections=[dict(rva=0x1000, size=0x1000, characteristics=0x60000020)],
        seeds=[0x1000, 0x1100, 0x1200],
        named_seeds=[dict(rva='0x1000', name='FixtureCaller'),
                     dict(rva='0x1100', name='FixtureCallee'),
                     dict(rva='0x1200', name='FixtureFragmented')],
        unwind_hints=[[0x1000, 0x1011], [0x1200, 0x1209]],
        caveat='Synthetic fixture. No game data.')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    print(output / 'manifest.json')


if __name__ == '__main__':
    make_fixture()
