"""Read-only disk MSVC RTTI inventory; static metadata only, no code claims."""
import json, re, struct
from pathlib import Path
import pefile

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(r'F:\SteamLibrary\steamapps\common\ForzaHorizon4\ForzaHorizon4.exe')
data = EXE.read_bytes()
pe = pefile.PE(data=data, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
sections = [(s.PointerToRawData, s.PointerToRawData + s.SizeOfRawData, s.VirtualAddress, s.Name.rstrip(b'\0').decode(), s.Characteristics) for s in pe.sections]

def off_rva(off):
    for start, end, rva, _, _ in sections:
        if start <= off < end:
            return off - start + rva
    raise ValueError(hex(off))

def rva_off(rva):
    for start, end, start_rva, _, _ in sections:
        if start_rva <= rva < start_rva + end - start:
            return rva - start_rva + start
    raise ValueError(hex(rva))

def read(rva, fmt):
    return struct.unpack_from(fmt, data, rva_off(rva))

def name(td):
    try:
        off = rva_off(td + 16)
        end = data.find(b'\0', off, off + 2048)
        return data[off:end].decode(errors='replace')
    except (ValueError, struct.error):
        return None

def hierarchy(chd):
    try:
        sig, attributes, count, array = read(chd, '<4I')
        if count > 256:
            return None
        result = []
        for bcd in read(array, '<' + 'I' * count):
            td, contained, mdisp, pdisp, vdisp, attrs, sub = read(bcd, '<IIiiiII')
            result.append(dict(name=name(td), type=hex(td), contained_bases=contained,
                               mdisp=mdisp, pdisp=pdisp, vdisp=vdisp, attributes=hex(attrs),
                               descriptor=hex(bcd), hierarchy=hex(sub)))
        return dict(attributes=hex(attributes), bases=result)
    except (ValueError, struct.error):
        return None

types = {}
for m in re.finditer(rb'\.\?A[VU][ -~]{1,1800}?@@\x00', data):
    try:
        types[off_rva(m.start()) - 16] = m.group()[:-1].decode()
    except ValueError:
        pass

pattern = re.compile(r'UIRenderer|UIRender|UIManager|HUD|Hud|hud|Menu|Screen|InWorld|World.*UI|UI.*World|Clip|Layout|Canvas|Instrument|NamePlate|Nameplate|Billboard|Waypoint|Destination|GamerTag|GamerCard|Gamer.*Name|UserInterface|UIPage|UIControl|ForzaScene|TiledScene|Telemetry|RenderTree|DrawingContext|ARControl|GameApplication|UIContext')
selected = {td: n for td, n in types.items() if (pattern.search(n) or '@AVUI@@' in n) and '?' not in n[4:]}
cols = {}
# Locate candidate COLs with cheap 4-byte signature indexing, validate self and known type.
for start, end, section_rva, secname, _ in sections:
    if secname.startswith('.text'):
        continue
    for at in range(start, end - 24, 4):
        if data[at:at+4] != b'\1\0\0\0':
            continue
        sig, offset, cd, td, chd, self_rva = struct.unpack_from('<6I', data, at)
        if td in selected and self_rva == section_rva + at - start:
            cols[self_rva] = dict(name=selected[td], type=hex(td), col=hex(self_rva),
                                 object_offset=offset, construction_displacement=cd,
                                 hierarchy_rva=hex(chd), hierarchy=hierarchy(chd), vtables=[])

for start, end, section_rva, secname, _ in sections:
    if secname.startswith('.text'):
        continue
    for at in range(start, end - 8, 8):
        value = struct.unpack_from('<Q', data, at)[0] - base
        if value not in cols:
            continue
        methods = []
        for i in range(512):
            ptr = struct.unpack_from('<Q', data, at + 8 + i*8)[0] - base
            executable = any(sr <= ptr < sr + en - st and flags & 0x20000000 for st,en,sr,_,flags in sections)
            if not executable:
                break
            methods.append(hex(ptr))
        cols[value]['vtables'].append(dict(rva=hex(section_rva + at - start + 8), methods=methods))

result = dict(exe=str(EXE), image_base=hex(base), warning='RTTI/data evidence only. Executable section bytes may be encrypted. Method names and behavior are not inferred from addresses.',
              total_types=len(types), selected_types=[dict(type=hex(td),name=n) for td,n in selected.items()],
              classes=list(cols.values()))
(ROOT/'reports/analysis-ui-types-agent.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
priority = [
    '.?AVUserInterface@@', '.?AVForzaUIManager@@', '.?AVGameApplication@@',
    '.?AVHud@@', '.?AVPauseMenuTiled@@', '.?AVForzaScene@@', '.?AVTiledScene@@',
    '.?AVHUDInterface@@', '.?AVHUDInterfaceAR@@', '.?AVHUDInterfaceNotification@@',
    '.?AVHUDModule@EntityModel@@', '.?AVHUDModuleAR@EntityModel@@',
    '.?AVUIBillboardModule@EntityModel@@', '.?AUUIRenderTargetRequest@EntityModel@@',
    '.?AVUIContextModule@EntityModel@@', '.?AVRenderer@AVUI@@', '.?AVMediaContext@AVUI@@',
    '.?AVRenderData@AVUI@@', '.?AVVisualDrawingContext@AVUI@@',
    '.?AUDrawTextCmd@RenderData@AVUI@@', '.?AUDrawGeometryCmd@RenderData@AVUI@@',
    '.?AVVisual@AVUI@@', '.?AVUIElement@AVUI@@', '.?AVUIRenderer@@',
    '.?AVIClientRendererInterface@AVUI@@', '.?AVIForzaUIRenderer@@'
]
lines = [f'total types={len(types)} selected={len(selected)} COLs={len(cols)}',
    'Evidence: PE disk RTTI records validated by COL signature=1, self RVA, type descriptor, and executable-section vtable entries.',
    'WARNING: executable bytes are encrypted/obfuscated on disk; the table does NOT identify method semantics or signatures.',
    'A slot number is zero-based. Offsets listed in hierarchy are base-subobject offsets, NOT inferred data fields.',
    '', 'CONCLUSIONS / LIMITS',
    '1. UIRenderer -> IForzaUIRenderer -> AVUI::IClientRendererInterface is proven by RTTI. The concrete table has 79 slots; the base AVUI interface has 62.',
    '2. Hud -> ForzaScene -> UIPage -> AVUI::Page and PauseMenuTiled -> TiledScene -> ForzaSceneWithBack -> ForzaScene are proven inheritance paths. They identify screen root candidates.',
    '3. HUDInterface and HUDInterfaceAR are different concrete implementations of ForzaUI::IHUDInterface. HUDModule and HUDModuleAR also have separate vtables. Their names strongly suggest an ordinary/AR distinction, but exact visual coverage is NOT proven.',
    '4. AVUI::RenderData implements IDrawingContent at +0x20. That secondary vtable is 0x5c0c208; candidate methods are 0x3c27174, 0x3c602b0, 0x3c3a5a0. This is a display-list scope candidate, NOT yet a known complete-screen boundary.',
    '5. UIContextModule, UIBillboardModule and UIRenderTargetRequest may help classify offscreen/material content. No target-size heuristic is implied by these names.',
    '6. Disk bytes at known live function 0x3e06720 begin af ac dd ee... while the recorded live function begins mov [rsp+8],rbx. Do NOT disassemble disk code as if it matched the runtime.',
    '', 'PRIORITY TYPES']
for n in priority:
    matches = [c for c in cols.values() if c['name'] == n]
    if not matches:
        td = [hex(td) for td, name in types.items() if name == n]
        lines.append(f'{n}: type descriptors={td}; no COL/vtable found (an abstract base may lack one).')
        continue
    for c in matches:
        lines.append(f"{c['name']} TD={c['type']} COL={c['col']} subobject offset={hex(c['object_offset'])}")
        if c['object_offset'] == 0 and c['hierarchy']:
            lines.append('  inheritance: ' + '; '.join(f"{b['name']} @ {hex(b['mdisp'])}" for b in c['hierarchy']['bases']))
        for v in c['vtables']:
            lines.append(f"  vtable={v['rva']} slots={len(v['methods'])}")
            lines.append('  ' + ' '.join(f'{i}:{m}' for i,m in enumerate(v['methods'])))
    lines.append('')
by_name = {c['name']:c for c in cols.values() if c['object_offset'] == 0}
lines.append('OVERRIDE DELTAS (candidate method identities only; not semantic names)')
for derived, parent in [('Hud','ForzaScene'), ('PauseMenuTiled','TiledScene'), ('GameApplication','Application@AVUI')]:
    a = by_name['.?AV'+derived+'@@']['vtables'][0]['methods']
    b = by_name['.?AV'+parent+'@@']['vtables'][0]['methods']
    changes = [(i, value, b[i] if i < len(b) else None) for i, value in enumerate(a) if i >= len(b) or value != b[i]]
    lines.append(f'{derived} vs {parent}: {changes}')
lines.extend(['', 'INDEX OF SELECTED TYPES / VTABLES'])
for c in cols.values():
    vs = ', '.join(v['rva'] + '/' + str(len(v['methods'])) for v in c['vtables'])
    bases = c['hierarchy']['bases'] if c['hierarchy'] else []
    lines.append(f"{c['name']} TD={c['type']} COL={c['col']} object_offset={c['object_offset']} vtables={vs}")
(ROOT/'reports/analysis-ui-types-agent.txt').write_text('\n'.join(lines), encoding='utf-8')
print(lines[0])
print('saved reports/analysis-ui-types-agent.json and .txt')
