"""Read only the archived baseline map diagnostic; write offline analysis."""
from pathlib import Path
import collections,json,struct
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'reports/ui-coloronly-map-startup-49064/ui-map-probe.log'
rows=[]
def matrix(s):return np.array([float(x) for x in s.split(',')],dtype=np.float64).reshape(4,4)
def affine(m):return np.max(np.abs(m[:,3]-[0,0,0,1]))<1e-7
def native_planes(c,rect):
    inv=np.linalg.inv(c);x,y,w,h=rect
    planes=[]
    for p in ([0,1,0,y],[1,0,0,x],[0,-1,0,-y-h],[-1,0,0,-x-w]):
        n=inv[:3,:3]@np.asarray(p[:3])
        pt=np.asarray(p[:3])*p[3]@c[:3,:3]+c[3,:3]
        planes.append([*n,np.dot(pt,n)])
    return np.array(planes)
for line in SRC.read_text().splitlines():
    d=dict(x.split('=',1) for x in line.split() if '=' in x)
    if d.get('pid')!='49064' or d.get('vr_active')!='0':continue
    if d.get('phase')!='return' or d.get('map_cb_valid')!='1':continue
    cb=np.array([float(x) for x in d['map_cb_c0'].split(',')])
    mv=cb[:16].reshape(4,4);mvp=cb[16:32].reshape(4,4);planes=cb[32:48].reshape(4,4)
    c=matrix(d['combined3e0']);P=matrix(d['P360']);B=matrix(d['B2e0']);A=matrix(d['A2a0']);D=matrix(d['D3a0']);L=matrix(d['L320']);cache=matrix(d['clip470'])
    packed=bytes.fromhex(d['clip_top']);rect=list(struct.unpack_from('<4f',packed));enabled=packed[16]
    k=mv@np.linalg.inv(c)
    r=dict(tick=int(d['tick']),sequence=int(d['sequence']),node=d['node'],map_view=d['map_view'],
        root=int(d['root']),root_match=int(d['root_match']),context_match=int(d['context_match']),
        clip_requested=int(d['clip_requested']),size=d['size'],layer=d['layer'],rect=rect,rect_enabled=enabled,
        p_affine=bool(affine(P)),c_affine=bool(affine(c)),mv_affine=bool(affine(mv)),
        mv_w=mv[:,3].tolist(),factored_k_w=k[:,3].tolist(),planes_cache_error=float(np.max(abs(planes-cache))),
        c_bpa_error=float(np.max(abs(c-B@P@A))),mvp_mvd_error=float(np.max(abs(mvp-mv@D))),
        mvp_mvd_relative=float(np.max(abs(mvp-mv@D)/(1+abs(mvp)))),
        native_planes_error=float(np.max(abs(cache-native_planes(c,rect)))) if enabled else None,
        k_factored=k.tolist(),mv=mv.tolist(),mvp=mvp.tolist(),planes=planes.tolist(),c=c.tolist(),D=D.tolist())
    rows.append(r)
summary=dict(source=str(SRC.relative_to(ROOT)),return_samples=len(rows),
    counts={key:dict(collections.Counter(str(r[key]) for r in rows)) for key in
            ['node','map_view','size','root_match','context_match','clip_requested','p_affine','c_affine','mv_affine']},
    max_c_bpa_error=max(r['c_bpa_error'] for r in rows),
    max_planes_cache_error=max(r['planes_cache_error'] for r in rows),
    max_native_planes_error=max(r['native_planes_error'] or 0 for r in rows),
    max_mvp_mvd_error=max(r['mvp_mvd_error'] for r in rows),
    max_mvp_mvd_relative=max(r['mvp_mvd_relative'] for r in rows),
    limitations=['Return snapshots are final CPU state, not atomic GPU upload captures.',
                'MV*inverse(C) is algebraic factorization, not a directly captured map view matrix.',
                'No actual map vertex buffer or live shader ID captured.'])
result=dict(summary=summary,samples=rows)
(ROOT/'reports/ui-map-baseline-49064-analysis.json').write_text(json.dumps(result,indent=2))
print(json.dumps(summary,indent=2))
for node in summary['counts']['node']:
    r=next(r for r in rows if r['node']==node)
    print('NODE',node,'size',r['size'],'rect',r['rect'],'MVw',r['mv_w'],'K',r['k_factored'])
