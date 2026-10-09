"""Skin collapse is a deformation proxy, invariant to rigid frames and source seams."""
import sys
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from isolate_native_mirror_skin import collapse_regions
before=np.array([[[0,0,0],[1,0,0],[0,1,0]],[[0,0,0],[2,0,0],[0,1,0]],[[0,0,0],[0,0,0],[0,0,0]]],dtype=float);after=before.copy();after[1,2]=[0,.01,0];ids=[dict(region='thumb',surface=0),dict(region='index',surface=2),dict(region='palm',surface=2)]
assert collapse_regions(before,before,ids)['region_counts']=={}
result=collapse_regions(before,after,ids);assert result['region_counts']=={'index':1}and result['surface_counts']=={'2':1}and result['triangles']==[ids[1]]
rotation=Rotation.from_rotvec([.4,-.3,.2]).as_matrix();translation=[3,4,5];assert collapse_regions(before@rotation.T+translation,after@rotation.T+translation,ids)==result
print('PASS: native-identity area has zero added collapse; region/material isolation ignores degenerate source triangles and rigid transforms')
