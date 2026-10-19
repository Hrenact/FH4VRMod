"""Extract UI descriptor names from an existing read-only runtime image dump."""
import bisect, json, re, struct
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

root = Path(__file__).resolve().parents[1]
folder = root/'reports/ui-runtime-16224'
data = (folder/'fh4-image.bin').read_bytes()
meta = json.loads((folder/'manifest.json').read_text())
base = meta['runtime_base']
hints = {a:b for a,b in meta['unwind_hints']}
source = (folder/'decompiled/0174d770_ForzaUIManager_slot5.c').read_text()
matches = re.findall(r'case (0x[0-9a-f]+|[0-9]+):\s*func_0x([0-9a-f]+)\(param_2\)', source)
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

def get_string(rva):
    if rva < 0 or rva >= len(data): return None
    raw=data[rva:rva+1024]
    if len(raw)>3 and raw[1]==0:
        end=next((i for i in range(0,len(raw)-1,2) if raw[i:i+2]==b'\0\0'),None)
        if end is None: return None
        try: s=raw[:end].decode('utf-16le')
        except UnicodeDecodeError: return None
    else:
        end=raw.find(b'\0')
        if end<0: return None
        try: s=raw[:end].decode('ascii')
        except UnicodeDecodeError: return None
    return s if len(s)>=3 and all(' '<=c<='~' for c in s) else None

lines=['UI semantic classification research, runtime image PID 16224.',
       'Inputs: immutable fh4-image.bin (base 0x7ff70cb10000), Ghidra .c/.asm outputs, validated RTTI.',
       'No process reads/writes, game hooks, or Ghidra project changes performed by this script.',
       '',
       'VERIFIED SEMANTICS AND CORRECTIONS',
       'HUDInterface slot0 / HUDInterfaceAR slot0 are destructors. Their slots1/2 lock weak module references at +0x20/+0x28 and forward status-like objects, NOT draw calls.',
       'Ordinary slots1/2 use module virtual slots +0x58/+0x60 (1202fc0 / 120fb20); AR uses 1202ed0 / 120fa30. Each pair then calls the SAME shared helper: 1272860 for first operation and 127bdf0 for second. AR-vs-HUD RTTI names alone are not render-pass proof.',
       'AVUI RenderData::IDrawingContent slot1 (3c602b0) is a bounds/data-address getter returning this+0x28; slot2 (3c3a5a0) iterates command records calling their virtual +0x50 and exits on true. It is NOT established as an entire UI drawing hook.',
       'ForzaUIManager slot0 (16b3100) creates/registers a page under integer ID in map +0x18; it writes page+0x27d=1. Slot1 erases matching IDs, slot2 queries IDs, slot5 builds one of 453 descriptors, slot6 finds descriptors by name.',
       'UIPage fields verified in Hud/Pause/Opponent factories: +0x198 integer ID; +0x1a0 ASCII name string; +0x1c0 UTF16 resource path; +0x1e0 type metadata; +0x1e8 factory callback; +0x1f0 extra string.',
       'Hud factory16dca80 allocates0x410 then calls ctor1c573e0. Pause factory16e3870 allocates0xdd0 then calls ctor1d54220. Opponent factory16e3010 allocates0x460 then calls ctor1d53bf0; its primary vtable50dbd70 resolves through RTTI to OpponentLabel.',
       'UserInterface+0xd0 stores renderer interface: 3abd230 invokes renderer virtual +0x58 to set projection.',
       '',
       'WINDOW-LEVEL CLASSIFICATION (distinct from UIPage object)',
       'WindowLayerManager primary vtable5b9af98 has36 slots. Its linked-list sentinel is +0xb8, count/availability +0xc0, iteration cursor +0xc8.',
       'slot31/start iterator=3aab4e0 sets +0xc8 from +0xb8.',
       'slot32=3ab0720 returns only windows where state helper3bd1600(window)==2 and int(window+0x1b8)==6.',
       'slot34=3ab07e0 returns only windows where state==2 and kind(window+0x1b8)==1.',
       'slot29=3ab08c0 SKIPS kinds0,1,6 and returns other kinds. Exact names of the numeric enum remain unproven.',
       'UserInterface slot30=3ab96d0 calls iterator+0x100 (kind6), reads window+0x198/19c as FLOAT dimensions, checks +0x26d, binds a separate render destination. Material/offscreen path hypothesis is strong but visual coverage still needs validation.',
       'UserInterface slot28=3ab9c90 calls iterator+0x110 (kind1), reads window+0x1f0 as16-float transform and composes it with current view transform. World/AR path hypothesis is strong but should be validated with known OpponentLabel/AR roots.',
       'UserInterface slot31=3aba280 is a shared-reference wrapper CALLING3aba340. The latter invokes iterator+0xe8 at3aba543 and3abab13, excluding kind1/kind6. Earlier linear scan attributed these instructions to the wrapper; Ghidra+assembly corrected the boundary.',
       '3aba340 loops outer viewport records of stride0xa0. Inside, kind4 is filtered by viewport index at window+0x268; kind5 has weak-object/associated-object and matrix processing. Thus this is a regular-window scope candidate, NOT proof that every returned item is fixed HUD. Do not indiscriminately move kind5.',
       'Ghidra renders the 3aba280->3aba340 first argument as0, but assembly at3aba2ea leaves RCX from the entry intact. Verify prototype against assembly; never transcribe that0.',
       'WARNING: UIPage+0x198 is ID, UIWindow+0x198 is width. Their +0x1f0 fields also differ. Do not apply offsets based only on a generic UI pointer.',
       'The original instrument panel capture and a world-space house icon explain why capturing low-level UIRenderer calls mixes classes. Preserve type/kind information at the window/page scope before touching render targets.',
       '',
       'UI DESCRIPTOR CATALOG',
       'UI descriptor metadata extracted from ForzaUIManager slot5 dispatch.',
       'Evidence: integers -> direct constructors -> RIP-relative readable string addresses.',
       'Executable RIP references are candidates, NOT assigned function names/signatures.', '']
for typ in ['.?AVWindowLayerManager@@', '.?AVUIWindow@@']:
    td=data.find(typ.encode()+b'\0')-16
    cursor=0
    while True:
        at=data.find(struct.pack('<I',td),cursor)
        if at<0: break
        cursor=at+1
        col=at-12
        if col<0 or struct.unpack_from('<I',data,col)[0]!=1: continue
        sig,offset,cd,typ_rva,chd,self_rva=struct.unpack_from('<6I',data,col)
        if self_rva!=col: continue
        vt=data.find(struct.pack('<Q',base+col))+8
        if vt==7: continue
        methods=[]
        for i in range(256):
            fn=struct.unpack_from('<Q',data,vt+i*8)[0]-base
            if not 0x1000<=fn<0x4bb0000: break
            methods.append(hex(fn))
        lines.append(f'TYPE {typ} offset={hex(offset)} vtable={hex(vt)} methods='+str(list(enumerate(methods))))
lines.append('')
for key,target in matches:
    rva=int(target,16)-base
    end=hints.get(rva,rva+0x600)
    strings=[]; funcs=[]
    for inst in md.disasm(data[rva:min(end,rva+0x5000)],rva):
        if inst.mnemonic != 'lea': continue
        for op in inst.operands:
            if op.type != X86_OP_MEM or op.mem.base != X86_REG_RIP: continue
            loc=inst.address+inst.size+op.mem.disp
            s=get_string(loc)
            if s and loc>=0x4bb0000: strings.append((hex(loc),s))
            if 0x1000 <= loc < 0x4bb0000: funcs.append(hex(loc))
    lines.append(f'id={int(key,0)} ({hex(int(key,0))}) constructor={hex(rva)} strings={strings} executable_refs={funcs}')
(root/'reports/ui-decompile-types-16224.txt').write_text('\n'.join(lines),encoding='utf-8')
print(f'extracted {len(matches)} descriptors')
print('\n'.join(line for line in lines if re.search(r'Hud|HUD|Pause|AR|Instrument|Telemetry|Nameplate|PlayerName|Billboard',line)))
