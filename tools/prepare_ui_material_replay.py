"""Convert captured matrices into offline proxy-quad GPU replay cases.

This deliberately does not claim to recover the original mesh, AA fringe,
material textures/PS, UV transform or Acrylic localEyePos. Geometry is a unit
quad with NORMAL=0; reference geometry is clipped on the CPU in source space.
"""
import argparse
import json
from pathlib import Path
import struct
import numpy as np

def polygon_clip(points, plane, tolerance=1e-3):
    if not points: return []
    def distance(p):
        d = float(np.array([p[0], p[1], 0, 1]) @ plane)
        return 0.0 if abs(d) <= tolerance else d
    result=[]; previous=points[-1]; before=distance(previous)
    for point in points:
        after=distance(point)
        if (before>=0) != (after>=0):
            t=before/(before-after);result.append([previous[0]+t*(point[0]-previous[0]),previous[1]+t*(point[1]-previous[1])])
        if after>=0:result.append(point)
        previous,before=point,after
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--inset',type=float,default=0,help='Counterfactual stress clip inset fraction; matrices stay captured, planes are regenerated')
    parser.add_argument('--local-z',type=float,default=None,help='Counterfactual affine local-Z translation; regenerate MV with native SSE float association')
    args=parser.parse_args(); rows=json.loads(args.input.read_text()); result=bytearray(b'FH4MREP1'+struct.pack('<I',len(rows))); metadata=[]
    assert 0 <= args.inset < .5
    for index,row in enumerate(rows):
        meta=row['metadata']; matrices=row['matrices']; read=lambda key:np.array(matrices[key],dtype=np.float32).reshape(4,4)
        mv,q1,q2,c=map(read,['packet_m0','packet_m1','packet_m2','combined3e0'])
        if args.local_z is not None:
            local=mv.astype(np.float64)@np.linalg.inv(c.astype(np.float64));local[:3,3]=0;local[3,3]=1;local[3,2]=args.local_z
            local=local.astype(np.float32);new_mv=np.zeros((4,4),dtype=np.float32)
            for r in range(4):
                for column in range(4):
                    new_mv[r,column]=(local[r,3]*c[3,column]+local[r,1]*c[1,column])+(local[r,2]*c[2,column]+local[r,0]*c[0,column])
            mv=new_mv
        raw=np.frombuffer(bytes.fromhex(row['packet_raw50']),dtype='<f4').reshape(4,4)
        rect=np.array(meta['clip_rect'][:4],dtype=np.float32)
        if args.inset:
            rect[0]+=rect[2]*args.inset;rect[1]+=rect[3]*args.inset;rect[2:]*=1-2*args.inset
        x,y,w,h=map(float,rect)
        if args.inset:
            # Counterfactual coefficients follow native 37f9d20, not captured data.
            inv=np.linalg.inv(c.astype(np.float64)); regenerated=np.zeros((4,4))
            for n,plane in enumerate([[0,1,0,y],[1,0,0,x],[0,-1,0,-y-h],[-1,0,0,-x-w]]):
                plane=np.array(plane);normal=inv[:3,:3]@plane[:3]
                point=c[3,:3]+(plane[3]*plane[:3])@c[:3,:3]
                regenerated[n,:3]=normal;regenerated[n,3]=point@normal
            raw=regenerated.astype(np.float32)
        # Inverse in double for reference construction, not the candidate path.
        local=mv.astype(np.float64)@np.linalg.inv(c.astype(np.float64))
        source_planes=np.array([[0,1,0,-y],[1,0,0,-x],[0,-1,0,y+h],[-1,0,0,x+w]],dtype=np.float64)
        points=[[0,0],[1,0],[1,1],[0,1]] if w>0 and h>0 else []
        for plane in source_planes: points=polygon_clip(points,local@plane)
        triangle=[]
        for n in range(1,len(points)-1):triangle.extend([points[0],points[n],points[n+1]])
        name=f'{index:03}_{meta["preset"]}_{meta["capture_tick"]}'+('_inset' if args.inset else '')+('_localz' if args.local_z is not None else '')
        encoded=name.encode('ascii');assert len(encoded)<96
        result+=encoded+bytes(96-len(encoded));result+=rect.tobytes()
        size=list(map(float,meta['size'].split(',')));result+=struct.pack('<2f',*size)
        for matrix in [mv,q1,q2,c,raw]:result+=matrix.astype('<f4').tobytes()
        result+=struct.pack('<I',len(triangle))
        for point in triangle:result+=struct.pack('<2f',*point)
        corners=np.array([[0,0,0,1],[1,0,0,1],[1,1,0,1],[0,1,0,1]],dtype=np.float64)
        clip=corners@mv.astype(np.float64)@q1.astype(np.float64);view=corners@mv.astype(np.float64)
        metadata.append({'name':name,'metadata':meta,'replay_clip_rect':rect.tolist(),'counterfactual_inset':args.inset,'counterfactual_local_z':args.local_z,'proxy_local_matrix':local.tolist(),'reference_polygon':points,
                         'reference_vertices':len(triangle),'mv_w_range':[float(view[:,3].min()),float(view[:,3].max())],
                         'clip_w_range':[float(clip[:,3].min()),float(clip[:,3].max())],
                         'depth_range':[float((clip[:,2]/clip[:,3]).min()),float((clip[:,2]/clip[:,3]).max())]})
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(result)
    args.output.with_suffix('.json').write_text(json.dumps({'source':str(args.input),'kind':'captured matrices, synthetic unit proxy quad, constant PS',
        'reference_source_distance_snap_tolerance':1e-3,'counterfactual_inset':args.inset,'counterfactual_local_z':args.local_z,'cases':metadata},indent=2)+'\n')
    print(f'Wrote {len(rows)} proxy cases, {len(result)} bytes, to {args.output}')

if __name__=='__main__':main()
