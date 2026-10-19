"""Offline read-only PrepareDraw routing audit of the saved PID16224 image."""
import json
import struct
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM

root = Path(__file__).resolve().parents[1]
folder = root / 'reports/ui-runtime-16224'
data = (folder / 'fh4-image.bin').read_bytes()
manifest = json.loads((folder / 'manifest.json').read_text())
pdata = next(section for section in manifest['sections'] if section['name'] == '.pdata')
decoder = Cs(CS_ARCH_X86, CS_MODE_64)
decoder.detail = True
rows = []
scanned = 0
for offset in range(pdata['rva'], pdata['rva'] + pdata['size'] - 11, 12):
    begin, end, unwind = struct.unpack_from('<III', data, offset)
    if not (0x3df0000 <= begin < end <= 0x3e10000):
        continue
    scanned += 1
    calls, reads470, writes5b0 = [], [], []
    instructions = list(decoder.disasm(data[begin:end], begin))
    for index, item in enumerate(instructions):
        operands = [op for op in item.operands if op.type == X86_OP_MEM]
        if item.mnemonic in ('call', 'jmp') and any(op.mem.disp == 0x160 for op in operands):
            calls.append([f'{i.address:08x}: {i.mnemonic} {i.op_str}'
                          for i in instructions[max(0, index - 4):index + 1]])
        # Some bundled Capstone builds leave memory access flags zero on SSE
        # MOVUPS. Use Intel destination/source order for these move instructions.
        if item.mnemonic.startswith('mov') and any(op.type == X86_OP_MEM and op.mem.disp == 0x470 for op in item.operands[1:]):
            reads470.append(hex(item.address))
        if item.mnemonic.startswith('mov') and item.operands and item.operands[0].type == X86_OP_MEM and item.operands[0].mem.disp == 0x5b0:
            writes5b0.append(hex(item.address))
    if calls or reads470 or writes5b0:
        rows.append(dict(begin=hex(begin), end=hex(end), indirect160=calls,
                         reads470=reads470, writes5b0=writes5b0))
result = dict(input='ui-runtime-16224/fh4-image.bin', functions_scanned=scanned,
              caveat='Bounded UIRenderer range only; 0x160 can also be a D3D command-list method.',
              rows=rows)
target = root / 'reports/ui-prepare-audit-evidence-16224.json'
target.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(dict(functions_scanned=scanned, candidate_rows=len(rows),
                      both=[r['begin'] for r in rows if r['reads470'] and r['writes5b0']],
                      writes5b0=[r['begin'] for r in rows if r['writes5b0']])))
