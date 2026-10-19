"""Build offline proxy geometry from actual active MapCB/C/rectangle snapshots.

No vertices are read from the game. The reference shader receives the factored
native map transform K and source-space clip planes while retaining actual MVP.
"""
from pathlib import Path
import json,struct,collections
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/'reports/ui-coloronly-map-trial-49064/ui-map-probe.log'
def matrix(s):return np.asarray([float(x) for x in s.split(',')],dtype=np.float32).reshape(4,4)
def affine(c):
    a=c.copy();a[:,3]=[0,0,0,1];return a
def sourceplanes(rect):
    x,y,w,h=rect
    return np.asarray([[0,1,0,y],[1,0,0,x],[0,-1,0,-y-h],[-1,0,0,-x-w]],dtype=np.float64)
def planes(c,rect):
    src=sourceplanes(rect);src[:,3]*=-1
    p=(np.linalg.inv(c)@src.T).T;p[:,3]*=-1
    return p
def nativeplanes(c,rect):
    inv=np.linalg.inv(c);result=[]
    for p in sourceplanes(rect):
        n=inv[:3,:3]@p[:3];pt=p[:3]*p[3]@c[:3,:3]+c[3,:3]
        result.append([*n,np.dot(pt,n)])
    return np.asarray(result)
rows=[]
for line in SRC.read_text().splitlines():
    d=dict(x.split('=',1) for x in line.split() if '=' in x)
    if not all(d.get(k)==v for k,v in dict(pid='49064',phase='return',vr_active='1',
        map_cb_valid='1',projection_match='1',root_match='1').items()):continue
    if d.get('clip_requested')!='1':continue
    packed=bytes.fromhex(d['clip_top']);rect=np.asarray(struct.unpack_from('<4f',packed),dtype=np.float64)
    if not packed[16] or min(rect[2:])<=0:continue
    cb=np.asarray([float(x) for x in d['map_cb_c0'].split(',')],dtype=np.float32)
    c=matrix(d['combined3e0']).astype(np.float64);mv=cb[:16].reshape(4,4).astype(np.float64)
    mvp=cb[16:32].reshape(4,4);cache=matrix(d['clip470']);raw=cb[32:48].reshape(4,4)
    k=mv@np.linalg.inv(c);ca=affine(c);candidate=k@ca
    rows.append(dict(tick=int(d['tick']),sequence=int(d['sequence']),node=d['node'],size=d['size'],
        rect=rect.tolist(),c=c.tolist(),mv=mv.tolist(),mvp=mvp.tolist(),k=k.tolist(),candidate=candidate.tolist(),
        raw=raw.tolist(),screen=cb[49:51].tolist(),
        cache_error=float(np.max(abs(cache-raw))),native_plane_error=float(np.max(abs(raw-nativeplanes(c,rect)))),
        c_condition=float(np.linalg.cond(c)),affine_c_condition=float(np.linalg.cond(ca)),
        native_k_w=k[:,3].tolist()))

def vertices(k,rect,height):
    x,y,w,h=rect
    uv=[(x-.2*w,y-.2*h),(x+1.2*w,y-.2*h),(x+1.2*w,y+1.2*h),(x-.2*w,y+1.2*h)]
    ys=[0]*4 if height=='flat' else [40,-25,75,10]
    result=[]
    for (u,v),yh in zip(uv,ys):
        lhs=np.array([[k[0,0]-u*k[0,3],k[2,0]-u*k[2,3]],
                      [k[0,1]-v*k[0,3],k[2,1]-v*k[2,3]]])
        rhs=np.array([u*(yh*k[1,3]+k[3,3])-yh*k[1,0]-k[3,0],
                      v*(yh*k[1,3]+k[3,3])-yh*k[1,1]-k[3,1]])
        xp,zp=np.linalg.solve(lhs,rhs);result.append([xp,yh,zp])
    return np.array([result[i] for i in [0,1,2,0,2,3]],dtype=np.float32)

cases=[]
for r in rows:
    k=np.array(r['k']);c=np.array(r['c']);ca=affine(c);rect0=np.array(r['rect'])
    for height in ['flat','varying']:
        verts=vertices(k,rect0,height)
        for mode in ['actual','inset']:
            rect=rect0.copy()
            if mode=='inset':rect[:2]+=.15*rect[2:];rect[2:]*=.7
            raw=np.array(r['raw']) if mode=='actual' else nativeplanes(c,rect)
            case=dict(name=f"s{r['sequence']}_{height}_{mode}",sequence=r['sequence'],height=height,clip_mode=mode,
                rect=rect.tolist(),vertices=verts.tolist(),k_w_at_vertices=(np.column_stack([verts,np.ones(6)])@k[:,3]).tolist(),
                matrices={name:r[name] for name in ['mv','mvp','c','k','candidate']},
                planes_source=sourceplanes(rect).tolist(),planes_native=raw.tolist(),
                planes_full=planes(c,rect).tolist(),planes_candidate=planes(ca,rect).tolist(),screen=r['screen'])
            cases.append(case)
out=ROOT/'reports/ui-map-replay-49064';out.mkdir(exist_ok=True)
summary=dict(input=str(SRC.relative_to(ROOT)),active_clip_snapshots=len(rows),case_count=len(cases),
    nodes=dict(collections.Counter(r['node'] for r in rows)),sizes=dict(collections.Counter(r['size'] for r in rows)),
    max_cache_error=max(r['cache_error'] for r in rows),max_native_plane_error=max(r['native_plane_error'] for r in rows),
    max_c_condition=max(r['c_condition'] for r in rows),max_affine_c_condition=max(r['affine_c_condition'] for r in rows),
    limits=['Synthetic map proxy vertices, not captured meshes.','Source-space K is factored from CPU return snapshots.',
            'Actual clip mode uses captured planes; inset recomputes the native algorithm for counterfactual smaller rect.'])
(out/'fixtures.json').write_text(json.dumps(dict(summary=summary,snapshots=rows,cases=cases),indent=2))
with (out/'fixtures.bin').open('wb') as f:
    f.write(b'FH4MAPR2');f.write(struct.pack('<I',len(cases)))
    for case in cases:
        f.write(case['name'].encode().ljust(96,b'\0'))
        f.write(np.asarray(case['rect'],dtype='<f4').tobytes())
        for key in ['mv','mvp','c','k','candidate']:
            f.write(np.asarray(case['matrices'][key],dtype='<f4').tobytes())
        for key in ['planes_source','planes_native','planes_full','planes_candidate']:
            f.write(np.asarray(case[key],dtype='<f4').tobytes())
        f.write(np.asarray(case['screen'],dtype='<f4').tobytes())
        f.write(np.asarray(case['vertices'],dtype='<f4').tobytes())
print(json.dumps(summary,indent=2))
