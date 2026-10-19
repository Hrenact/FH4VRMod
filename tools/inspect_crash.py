"""Extract exception registers and module identity from an x64 Windows minidump."""
import argparse
import json
from pathlib import Path
import struct
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('dump', type=Path)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--peek', type=lambda s:int(s,0), action='append', default=[], help='Read up to 64 bytes at a supplied captured address')
args = p.parse_args()
data = args.dump.read_bytes()
if data[:4] != b'MDMP':
    raise ValueError('Expected Windows minidump')
count, directory = struct.unpack_from('<II', data, 8)
streams = {}
for i in range(count):
    kind, size, rva = struct.unpack_from('<III', data, directory + i*12)
    streams[kind] = (rva, size)
modules = []
module_paths = []
off = streams[4][0]
for i in range(struct.unpack_from('<I', data, off)[0]):
    entry = off + 4 + i*108
    base, size = struct.unpack_from('<QI', data, entry)
    name_rva = struct.unpack_from('<I', data, entry+20)[0]
    length = struct.unpack_from('<I', data, name_rva)[0]
    full_name = data[name_rva+4:name_rva+4+length].decode('utf-16-le')
    name = full_name.split('\\')[-1]
    modules.append((base,size,name))
    module_paths.append(dict(base=hex(base),size=size,path=full_name))
def identify(address):
    for base,size,name in modules:
        if base <= address < base+size:
            return f'{name}+0x{address-base:x}'
    return None
off = streams[6][0]
exception_thread = struct.unpack_from('<I', data, off)[0]
code = struct.unpack_from('<I', data, off+8)[0]
address = struct.unpack_from('<Q', data, off+24)[0]
params = struct.unpack_from('<I', data, off+32)[0]
context_size, context = struct.unpack_from('<II', data, off+160)
regs = {name: struct.unpack_from('<Q', data, context+120+i*8)[0]
        for i,name in enumerate(['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15','rip'])}
memory = []
thread_stacks=[]
if 3 in streams:
    loc=streams[3][0]
    for i in range(struct.unpack_from('<I',data,loc)[0]):
        entry=loc+4+i*48
        tid=struct.unpack_from('<I',data,entry)[0]
        start,size,rva=struct.unpack_from('<QII',data,entry+24)
        if size and rva and rva+size<=len(data):
            memory.append((start,size,rva))
        if tid==exception_thread:
            thread_stacks.append(dict(start=hex(start),size=size,rva=rva,captured=bool(size and rva and rva+size<=len(data))))
if 5 in streams:
    loc = streams[5][0]
    for i in range(struct.unpack_from('<I', data, loc)[0]):
        start,size,rva = struct.unpack_from('<QII', data, loc+4+i*16)
        memory.append((start,size,rva))
if 9 in streams:
    loc = streams[9][0]
    ranges,rva = struct.unpack_from('<QQ', data, loc)
    for i in range(ranges):
        start,size = struct.unpack_from('<QQ', data, loc+16+i*16)
        memory.append((start,size,rva))
        rva += size
def read(address, amount):
    for start,size,rva in memory:
        if start <= address < start+size:
            return data[rva+address-start:rva+min(size,address-start+amount)]
    return b''
sys.path.insert(0, str(Path(__file__).resolve().parent.parent/'.deps'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
decoder = Cs(CS_ARCH_X86, CS_MODE_64)
thread_info = []
if 17 in streams:
    loc = streams[17][0]
    header_size, entry_size, entries = struct.unpack_from('<III', data, loc)
    for i in range(entries):
        entry = loc + header_size + entry_size*i
        tid = struct.unpack_from('<I', data, entry)[0]
        start_address = struct.unpack_from('<Q', data, entry+48)[0]
        if tid == exception_thread:
            thread_info.append(dict(thread_id=tid, start_address=hex(start_address), start_module=identify(start_address)))
tls_callbacks = []
for module_base, _, module_name in modules:
    if module_name.lower() != 'forzahorizon4.exe':
        continue
    header = read(module_base, 4096)
    if len(header) < 64:
        continue
    pe = struct.unpack_from('<I', header, 0x3c)[0]
    tls_rva = struct.unpack_from('<I', header, pe+24+112+9*8)[0]
    tls = read(module_base+tls_rva, 40)
    if len(tls) == 40:
        callback_array = struct.unpack_from('<Q', tls, 24)[0]
        for i in range(32):
            pointer = read(callback_array+i*8, 8)
            if len(pointer) != 8:
                break
            callback = struct.unpack('<Q', pointer)[0]
            if not callback:
                break
            tls_callbacks.append(dict(address=hex(callback), module=identify(callback)))
result = dict(exception_code=hex(code), exception_address=hex(address),
              exception_thread_id=exception_thread, exception_thread_info=thread_info,
              game_tls_callbacks=tls_callbacks, stream_types=sorted(streams),
              exception_module=identify(address),
              exception_parameters=[hex(struct.unpack_from('<Q',data,off+40+i*8)[0]) for i in range(min(params,15))],
              registers={name:dict(value=hex(value),module=identify(value)) for name,value in regs.items()},
              instructions_at_fault=[f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}' for ins in decoder.disasm(read(address,64),address)],
              probe_module_present=any(name.lower()=='camera_probe.dll' for _,_,name in modules))
result['dxgi_modules']=[m for m in module_paths if m['path'].lower().endswith('\\dxgi.dll')]
from collections import Counter
result['exception_thread_stacks']=thread_stacks
repeated=Counter()
for captured in thread_stacks:
    if not captured['captured']: continue
    stack_bytes=data[captured['rva']:captured['rva']+min(captured['size'],2*1024*1024)]
    for pos in range(0,len(stack_bytes)-7,8):
        value=struct.unpack_from('<Q',stack_bytes,pos)[0]
        label=identify(value)
        if label: repeated[(hex(value),label)]+=1
result['repeated_raw_stack_pointers']=[dict(address=addr,module=label,count=count) for (addr,label),count in repeated.most_common(20)]
result['raw_stack_caveat']='Pointer frequencies are not unwound stack frames or proof of call order.'
result['captured_address_checks']=[]
for requested in args.peek:
    contents=read(requested,64)
    result['captured_address_checks'].append(dict(address=hex(requested),bytes=contents.hex(' '),
                                                first_pointer_module=identify(struct.unpack_from('<Q',contents)[0]) if len(contents)>=8 else None))
result['stack_module_pointer_candidates']=[]
stack=read(regs['rsp'],256)
for slot in range(0,len(stack)-7,8):
    value=struct.unpack_from('<Q',stack,slot)[0]
    label=identify(value)
    if label:
        result['stack_module_pointer_candidates'].append(dict(stack_offset=hex(slot),value=hex(value),module=label))
for module_base,_,module_name in modules:
    if module_name.lower()=='forzahorizon4.exe':
        entry=module_base+0x327ce40
        body=read(entry,32)
        result['camera_entry_bytes']=body.hex(' ')
        result['camera_entry_instructions']=[f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}' for ins in decoder.disasm(body,entry)]
        if body[:1]==b'\xe9':
            relay=entry+5+struct.unpack_from('<i',body,1)[0]
            relay_body=read(relay,32)
            result['camera_entry_relay']=dict(address=hex(relay),module=identify(relay),bytes=relay_body.hex(' '))
args.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
