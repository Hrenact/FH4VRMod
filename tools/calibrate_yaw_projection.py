"""Fit known 5-degree camera yaw against static image correspondences."""
from pathlib import Path
import json, math, shutil
import cv2
import numpy as np
from scipy.optimize import least_squares

root=Path(__file__).resolve().parents[1]
dest=root/'reports'/'projection-captures-34952'
dest.mkdir(exist_ok=True)
images=[]
for index in range(1,4):
    path=Path(r'F:\SteamLibrary\steamapps\common\ForzaHorizon4')/f'fh4vr-capture-34952-{index}.png'
    shutil.copy2(path,dest/path.name)
    images.append(cv2.imread(str(path),0))
h,w=images[0].shape
sift=cv2.SIFT_create(nfeatures=15000)
report={}
for region,box in {'cockpit':(600,850,1960,1380),'outside':(850,400,2200,790)}.items():
    mask=np.zeros_like(images[0]);x0,y0,x1,y1=box;mask[y0:y1,x0:x1]=255
    features=[sift.detectAndCompute(im,mask) for im in images]
    for index in (1,2):
        ka,da=features[0];kb,db=features[index]
        matches=[a for a,b in cv2.BFMatcher().knnMatch(da,db,k=2) if a.distance<.7*b.distance]
        a=np.float64([ka[m.queryIdx].pt for m in matches]);b=np.float64([kb[m.trainIdx].pt for m in matches])
        H,valid=cv2.findHomography(a,b,cv2.RANSAC,1.5)
        a=a[valid.ravel()!=0];b=b[valid.ravel()!=0]
        result={'inliers':len(a),'homography':H.tolist()}
        if index==1:
            # Pixel-square pinhole, principal point at image center; try both yaw signs.
            def project(f,sign):
                x=a[:,0]-w/2;y=a[:,1]-h/2
                c=math.cos(math.radians(5));s=sign*math.sin(math.radians(5))
                denominator=c-s*x/f
                return np.column_stack(((c*x+s*f)/denominator+w/2,y/denominator+h/2))
            fits=[]
            for sign in (-1,1):
                fit=least_squares(lambda p:(project(p[0],sign)-b).ravel(),[1450.],bounds=([300.],[6000.]),loss='soft_l1',f_scale=.5)
                residual=np.linalg.norm(project(fit.x[0],sign)-b,axis=1)
                fits.append((float(np.median(residual)),float(fit.x[0]),sign))
            error,f,sign=min(fits)
            result.update(focal_px=f,yaw_sign=sign,median_residual_px=error,horizontal_deg=math.degrees(2*math.atan(w/(2*f))),vertical_deg=math.degrees(2*math.atan(h/(2*f))))
            result['candidate_median_errors_px']={axis:float(np.median(np.linalg.norm(project(dimension/2/math.tan(math.radians(53/2)),sign)-b,axis=1))) for axis,dimension in [('53_vertical',h),('53_horizontal',w)]}
            def centered_model(p):
                focal,cx,cy=p
                x=a[:,0]-cx;y=a[:,1]-cy
                c=math.cos(math.radians(5));s=sign*math.sin(math.radians(5))
                denominator=c-s*x/focal
                return np.column_stack(((c*x+s*focal)/denominator+cx,y/denominator+cy))
            fit_center=least_squares(lambda p:(centered_model(p)-b).ravel(),[f,w/2,h/2],bounds=([500,w*.25,h*.25],[4000,w*.75,h*.75]),loss='soft_l1',f_scale=.5)
            result['free_principal_point']={'focal_cx_cy':fit_center.x.tolist(),'median_residual_px':float(np.median(np.linalg.norm(centered_model(fit_center.x)-b,axis=1)))}
            fixed_f=h/2/math.tan(math.radians(53/2))
            delta=b-project(fixed_f,sign)
            result['53_vertical_residual']={'median_dx_dy':np.median(delta,axis=0).tolist(),'dx_percentiles':np.percentile(delta[:,0],[10,50,90]).tolist(),'dy_percentiles':np.percentile(delta[:,1],[10,50,90]).tolist()}
        else:
            result['median_return_displacement_px']=float(np.median(np.linalg.norm(a-b,axis=1)))
        report[f'{region}_1_to_{index+1}']=result
report['assumptions']='Known pure yaw5deg, centered pinhole, square pixels. Independent cockpit/exterior fits; HUD and mirror avoided. This measures this view/resolution only.'
(dest/'analysis.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
