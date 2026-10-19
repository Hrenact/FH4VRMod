"""Read-only audit of UI viewport/scissor calls in the immutable PID 16224 image."""
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
functions = []
for offset in range(pdata['rva'], pdata['rva'] + pdata['size'] - 11, 12):
    begin, end, unwind = struct.unpack_from('<III', data, offset)
    if 0x3df0000 <= begin < 0x3e10000 and begin < end <= 0x3e20000:
        functions.append((begin, end))

calls = []
for begin, end in functions:
    instructions = list(decoder.disasm(data[begin:end], begin))
    for index, instruction in enumerate(instructions):
        if instruction.mnemonic not in ('call', 'jmp'):
            continue
        if not any(operand.type == X86_OP_MEM and operand.mem.disp in (0xa8, 0xb0)
                   for operand in instruction.operands):
            continue
        calls.append({
            'function_rva': hex(begin), 'end_rva': hex(end),
            'call_rva': hex(instruction.address),
            'instructions': [f'{item.address:08x}: {item.mnemonic} {item.op_str}'
                             for item in instructions[max(0, index - 12):index + 3]],
        })

evidence = {'input': str(folder / 'fh4-image.bin'),
            'runtime_base': hex(manifest['runtime_base']),
            'function_ranges_scanned': len(functions),
            'range': ['0x3df0000', '0x3e10000'], 'indirect_a8_b0_calls': calls,
            'default_depth_stencil_words_at_5c7f470':
                list(struct.unpack_from('<4I', data, 0x5c7f470))}
(root / 'reports/ui-scissor-evidence-16224.json').write_text(
    json.dumps(evidence, indent=2), encoding='utf-8')

report = '''UI hardware viewport/scissor and pipeline audit, PID 16224
2026-10-19. Read-only offline research. No process access, hooks, game writes,
production edits, or new Ghidra process/project changes.

Inputs: immutable fh4-image.bin, existing Ghidra C/ASM, current local
src/ui_projection_test.h, Windows SDK 10.0.26100.0 D3D12 declarations.
All addresses below are module-relative RVAs. Source image is RVA-addressable.
The companion JSON records a bounded .pdata-guided raw disassembly sweep,
not a claim about every indirect call elsewhere in the entire game.

RESULT
======
No per-widget/projective scissor rectangle was found in the audited UIRenderer
implementation. Its one verified hardware RSSetScissorRects call sets the full
default render target rectangle. Begin/EndLayer change viewport and destination
but do not themselves change the scissor. A stale narrow scissor is therefore
not supported by this static evidence. External command-list users can still
change state; a live D3D state capture would be needed to exclude that globally.

A distinct, concrete candidate is hardware z clipping: the real UI PSO always
enables RasterizerState.DepthClipEnable, whereas the mod's projective Plane
changes x/y/w but retains the native z column. That combination can generate
a moving straight z=w boundary for UI carrying nonzero layer depth, despite
DepthEnable being false. This is a testable candidate, not a proven live root
cause, and differs from both scissor and the repaired SV_ClipDistance planes.

VERIFIED DIRECT CALLS
====================
UIRenderer command list is reached through [[renderer+0x68]+8]. On this object:
  vtable+0xa8 is RSSetViewports (21)
  vtable+0xb0 is RSSetScissorRects (22)
  vtable+0xc8 is SetPipelineState (25)
  vtable+0x170 is OMSetRenderTargets (46)

0x3dfd390 ResetTarget:
  0x3dfd41c calls RSSetViewports(1, renderer+0x1a8).
  Source is default target [[renderer+0x68]+0x10], width+0x80, height+0x84.
  Viewport = {0,0,width,height,0,1}; copied also to renderer+0x78..0x8f.
  0x3dfd468 calls RSSetScissorRects(1, stack rect).
  Rect = integer {0,0,width,height}; copied also to renderer+0x90..0x9f.
  Then binds target through 3e096d0, resets clip-stack count+0x27c, and invokes
  renderer virtual+0x130 (slot38), preserving native clip-reset semantics.

0x3dfc800 BeginLayer:
  Builds a layer-specific matrix at renderer+0x3a0 and creates/reuses target
  through 3e03c40. The target is stored at renderer+0x650.
  Saves prior matrix, layer rectangle/target, viewport into a 0x70-byte stack.
  0x3dfd2c2 calls RSSetViewports(1, renderer+0x1a8), populated from param6
  {x,y,width,height}; min/max depth remain 0/1.
  0x3dfd2ce calls 3e096d0 to bind that target. Clears color and optional DS.
  No RSSetScissorRects call. It inherits the full-default-target scissor.
  Small layers can have their own finite target/viewport boundary even without
  a small scissor. A sampling/caching issue here is separate from shader planes.

0x3e018b0 EndLayer:
  Finalizes layer texture via 3e0b470; restores matrix+0x3a0, layer+0x650,
  rectangle+0x658, and parent target (or default target if no parent layer).
  0x3e019b6 restores RSSetViewports from renderer+0x1a8 and updates cache+0x78.
  No RSSetScissorRects call and no scissor value in the saved 0x70-byte state.

MISLEADING NAMES / IDENTICAL VTABLE OFFSETS
========================================
3e096d0: actual target binder, writes renderer+0x70. Its direct commands are
OMSetRenderTargets and root CBV slot4. No viewport/scissor update here.
3e09850: copies a 2D affine transform to +0x4b0..+0x4cc, depth scalar to+0x4d0,
and creates local 4x4 L at +0x320..+0x35f. This is not a hardware viewport setter.
3e096a0: copies a supplied local 4x4 directly to+0x320.
3e0a4a0: binds/clears rendering context and command-list references, no RS calls.
3e0a880 call at3e0a9a3 and3dffe70 call at3dffea5 use +0xb0 on
[[renderer+0x20]+0x20], the device, with sampler description+CPU descriptor.
They are CreateSampler, not scissor. Receiver provenance is essential.
3e06fc0 has another device sampler route, with the same distinction.
UserInterface virtual+0xa8 is its orthographic projection setter; it is not a
command-list viewport call either.

PIPELINE AND DEPTH EVIDENCE
===========================
3e02eb0 gets PSO from3e01d80 then calls command-list+0xc8; no RS/clip update.
Text draw endpoint3e08c00 similarly calls3e01d80 and+0xc8 before DrawInstanced.
3e01d80 constructs a D3D12_GRAPHICS_PIPELINE_STATE_DESC of0x290 bytes and calls
ID3D12Device::CreateGraphicsPipelineState at3e025dd (device virtual+0x50).
Actual descriptor pointer is RSP+0x40. From its prologue, RBP=RSP+0x100.
Thus these assembly writes map exactly to descriptor fields:
  3e0253e [RBP+0x104]=3:  desc+0x1c4 Rasterizer.FillMode=SOLID.
  3e02548 [RBP+0x108]=1:  desc+0x1c8 CullMode=NONE, FrontCounterClockwise=0.
  3e02568 [RBP+0x11c]=1:  desc+0x1dc DepthClipEnable=TRUE;
                                 +0x1e0 MultisampleEnable=FALSE.
  There is no branch overriding DepthClipEnable in this builder.
  [RBP+0x130]=constant5c7f470 at3e023cb: desc+0x1f0 depth/stencil defaults
  {DepthEnable=0, DepthWriteMask=1, DepthFunc=4, StencilEnable=0}.
Depth/stencil fields can be overridden from render-state enum globals; stencil
uses masks255 and several compare/operation variants. Do not globally disable
stencil or native masks. Hardware depth clipping is active even when depth
testing is disabled and SV_ClipDistance has been corrected.

CURRENT PROJECTION AND A FALSIFIABLE PREDICTION
=============================================
src/ui_projection_test.h::Plane retains input[row*4+2] for each row except the
old optional depthTrial. It overwrites x,y,w with the anchored panel projection.
The local2D transform3e09850 places its separate depth argument into L[14]
(renderer+0x358), so vertices whose source position z=0 can still gain native
depth in L*C*D. Native text obtains L*C via1a88b00 and carries D separately;
geometry prepares M0=L*C and M1=M0*D. The relevant hardware z/w must be measured
after the full final position transform, not from raw P alone.

For a simple illustrative normalized depth d, yaw theta and panel horizontal
coordinate s in[-tan25deg,+tan25deg], the new w is approximately
cos(theta)+s*sin(theta). With theta=0.6 and d=0.8, the far z clip boundary is
s=(0.8-cos(0.6))/sin(0.6)=-0.04487, inside the visible panel. The left portion
can disappear progressively while the right remains. d=0.2 stays below the
entire w range and does not show this behavior. This demonstrates a mechanism
for different UI elements disappearing at different angles, not actual game
layer values. The side reverses with yaw. An all-zero-depth fixture misses it.

NEXT DISCRIMINATING READ/TEST
============================
1. For affected draws, record L/P/C/D or final M1 and representative primitive
   z/local depth, current shader kind, viewport+0x78, scissor+0x90, target+0x70
   and target dimensions. Test both w-z and z against0 at the observed edge.
2. Extend the existing actual-game-VS GPU comparison with native nonzero layer
   depth and final D, compare DepthClipEnable true/false under identical affine
   planes. This can confirm/exclude the separate hardware depth boundary offline.
3. If scissor is examined live, associate command list, target and enclosing UI
   scope; the renderer cache is not proof external code has not changed GPU state.
4. If hardware z is excluded, inspect finite cached-layer texture/viewport and
   AVUI CPU bounds/stencil at the responsible draw. Do not disable all clipping.

No recommendation to alter singular/hidden-panel guards: the observed hidden
projection is intentionally degenerate and those rejected samples are expected.
'''
(root / 'reports/ui-scissor-viewport-16224.txt').write_text(report, encoding='utf-8')
print(f'Wrote viewport/scissor report and JSON: {len(functions)} ranges, {len(calls)} matching calls')
