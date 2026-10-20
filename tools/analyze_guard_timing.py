"""Analyze deferred guard trace. Correlation only, not final pixel provenance."""
import argparse,csv,json,re
from pathlib import Path

def analyze(path):
    lines=path.read_text().splitlines()
    frequency=int(re.search(r"qpc_frequency=(\d+)",lines[0]).group(1))
    rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(lines[1:])]
    previous=None;pending=[];intervals=[]
    for r in rows:
        pending.append(r)
        if r['kind']!=3 or r['flags']!=0:continue
        if previous is not None:
            captures=[x for x in pending if x['kind']==1]
            rejects=[x for x in captures if not x['flags']&1]
            missing=set(range(previous['generation']+1,r['generation']))
            rejected={x['generation'] for x in rejects}
            times=[previous['qpc']]+[x['qpc'] for x in captures]+[r['qpc']]
            intervals.append({'end_tick':r['tick'],'generation':r['generation'],
                'gap_ms':(r['qpc']-previous['qpc'])*1000/frequency,
                'guard_rejected_captures':len(rejects),'missing_generations':len(missing),
                'missing_generations_with_guard_reject':len(missing&rejected),
                'sequence_changes':sum(bool(x['flags']&4) for x in rejects),
                'largest_cpu_capture_gap_ms':max((b-a)*1000/frequency for a,b in zip(times,times[1:]))})
        previous=r;pending=[]
    spikes=[x for x in intervals if x['gap_ms']>33.333]
    return {'header':lines[0],'events':len(rows),'successful_submission_intervals':len(intervals),
        'spikes_over_33ms':len(spikes),'spikes_with_guard_reject':sum(x['guard_rejected_captures']>0 for x in spikes),
        'spikes_without_guard_reject':sum(x['guard_rejected_captures']==0 for x in spikes),
        'worst_intervals':sorted(intervals,key=lambda x:x['gap_ms'],reverse=True)[:50],
        'limits':'CPU decision and successful Submit return timestamps only. Rejections explain withheld pairs but do not establish why game cycles became irregular. Capture gaps include other work/waits. No network latency measurement.'}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('trace',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=analyze(args.trace);text=json.dumps(result,indent=2)
    if args.output:args.output.write_text(text)
    print(text)
