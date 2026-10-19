"""Summarize callback stages and observed x64 MinHook jump-chain bytes."""
import argparse
import json
from pathlib import Path
import struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('trace',type=Path)
p.add_argument('--out',type=Path,required=True)
args=p.parse_args()
samples=json.loads(args.trace.read_text(encoding='utf-8'))
if not samples: raise ValueError('No telemetry samples')
relay_results=[]
trampoline_results=[]
for sample in samples:
    relay=sample.get('observed_relay_bytes')
    if relay and relay.startswith('ff2500000000'):
        destination=struct.unpack_from('<Q',bytes.fromhex(relay),6)[0]
        relay_results.append(destination==int(sample['detour'],16))
    trampoline=sample.get('observed_trampoline_bytes')
    if trampoline:
        data=bytes.fromhex(trampoline)
        # The exact prologue requires seven bytes before MinHook's absolute jump.
        if data[:7]==bytes.fromhex(sample['original_bytes'])[:7] and data[7:13]==bytes.fromhex('ff2500000000'):
            destination=struct.unpack_from('<Q',data,13)[0]
            trampoline_results.append(destination==int(sample['target'],16)+7)
result=dict(samples=len(samples),pid=samples[0]['pid'],
            entered_max=max(s['entered'] for s in samples),returned_max=max(s['returned'] for s in samples),
            matched_max=max(s['matched'] for s in samples),read_failures_max=max(s['read_failures'] for s in samples),
            target_byte_variants=len({s['observed_target_bytes'] for s in samples if s.get('observed_target_bytes')}),
            trampoline_byte_variants=len({s['observed_trampoline_bytes'] for s in samples if s.get('observed_trampoline_bytes')}),
            relay_decodable_samples=len(relay_results),relay_destinations_match=all(relay_results) if relay_results else None,
            trampoline_decodable_samples=len(trampoline_results),trampoline_returns_match=all(trampoline_results) if trampoline_results else None,
            final_process_exit_code=samples[-1]['process_exit_code'],
            caveat='Sampled evidence only. Independent counters may differ while a callback is in flight; no causality claim.')
if samples[0].get('mode')=='vtable':
    readable_slots=[s for s in samples if s.get('observed_slot_pointer')]
    readable_code=[s for s in samples if s.get('observed_target_bytes')]
    result.update(mode='vtable',readable_slot_samples=len(readable_slots),
                  slots_point_to_observer=all(s['observed_slot_pointer']==s['detour'] for s in readable_slots) if readable_slots else None,
                  original_code_unchanged=all(s['observed_target_bytes']==s['original_bytes'] for s in readable_code) if readable_code else None)
args.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
