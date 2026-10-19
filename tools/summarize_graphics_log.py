"""Summarize sampled wrapper logs; invocation counts are not measured FPS."""
import argparse
import json
from pathlib import Path
import re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
p.add_argument('--pid',type=int,required=True)
p.add_argument('--out',type=Path,required=True)
args=p.parse_args()
lines=[line for line in args.log.read_text(encoding='utf-8-sig').splitlines() if line.startswith(f'pid={args.pid} ')]
returns=[line for line in lines if re.search(r'wrapper_present1?_return',line)]
counts=[int(re.search(r'count=(\d+)',line)[1]) for line in returns]
failures=[line for line in lines if any(int(hr,16)&0x80000000 for hr in re.findall(r'\bhr=([0-9a-fA-F]{8})',line))]
ticks=[int(re.search(r'tick=(\d+)',line)[1]) for line in lines]
result=dict(pid=args.pid,log_lines=len(lines),logged_return_samples=len(returns),
    last_logged_present_count=max(counts,default=0),
    captured_span_ms=max(ticks)-min(ticks) if ticks else 0,
    failed_hresult_lines=failures,
    reentry_lines=[line for line in lines if 'reentry' in line],
    unknown_interface_lines=[line for line in lines if 'unknown_iid' in line],
    resize_lines=[line for line in lines if 'wrapper_resize' in line],
    last_resource_sample=next((line for line in reversed(lines) if 'resource=' in line),None),
    caveat='Logs sample successful returns periodically and record failures; they do not measure unique frames, pixel correctness, GPU copy or long-term stability.')
args.out.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result,indent=2))
