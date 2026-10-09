"""Rigid leaf fitting math; no acceptance of private deformation limits."""
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fit_authored_accessory_leaf import fit_rigid
points=np.array([[0.,0.,0.],[1,0,0],[0,1,0],[0,0,1]])
rotation=np.array([[0.,-1,0],[1,0,0],[0,0,1]])
targets=points@rotation.T+np.array([.2,-.3,.4]);weights=np.array([.1,.4,.2,.3])
fitted,_=fit_rigid(points,targets,weights)
assert np.max(np.abs(fitted[:3,:3]-rotation))<1e-12
assert np.max(np.abs((fitted[:3,:3]@points.T).T+fitted[:3,3]-targets))<1e-12
stretched=targets.copy();stretched[1]+=[0.,.3,0]
fit,_=fit_rigid(points,stretched,weights)
assert np.max(np.linalg.norm((fit[:3,:3]@points.T).T+fit[:3,3]-stretched,axis=1))>.05
for invalid in (np.zeros(4),np.array([1.,1.,1.,float('nan')])):
    try:fit_rigid(points,targets,invalid)
    except ValueError:pass
    else:raise AssertionError('Invalid leaf weights accepted')
print('PASS: exact rigid weighted recovery, incompatible-target residual and invalid influence rejection')
