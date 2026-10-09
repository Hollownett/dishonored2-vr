"""Matched cohort census separates source seams and remains rigid-frame invariant."""
import sys
from pathlib import Path
import numpy as np
from scipy.spatial.transform import Rotation
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from compare_empty_hand_contact_baselines import census,strain
from check_authored_hand_contacts import evaluate
tri=np.array([[[0,0,0],[1,0,0],[0,1,0]],[[.2,.2,-1],[.2,.2,1],[.8,.2,0]]],dtype=float);ids=[dict(surface=0,region='thumb'),dict(surface=1,region='index')]
result=census(tri,tri,ids);assert result['raw_noncoplanar_crossings']==1 and result['kept_crossings']==1 and result['fixed_source_seam_excluded_crossings']==0
metric=evaluate(tri,tri,ids);assert metric['introduced_triangle_crossings']==0 and metric['existing_crossings_retained']==1
seam_before=tri.copy();seam_before[1,0]=seam_before[0,0];result=census(seam_before,tri,ids);assert result['raw_noncoplanar_crossings']==1 and result['kept_crossings']==0 and result['fixed_source_seam_excluded_crossings']==1
rotation=Rotation.from_rotvec([.3,-.2,.1]).as_matrix();offset=np.array([5,7,-2]);assert census(seam_before@rotation.T+offset,tri@rotation.T+offset,ids)==result
native=strain(tri,tri);assert native['edge_length_ratio']['minimum']==1 and native['triangle_area_ratio']['minimum']==1 and native['triangles_below_one_tenth_source_area']==0
folded=tri.copy();folded[0,2]=[0,.01,0];assert strain(tri,folded)['triangles_below_one_tenth_source_area']==1
assert not result['coplanar_contacts_certified']and not native['skin_fold_acceptance_certified']
print('PASS: same-cohort native existing vs new crossings, fixed-source seam accounting, rigid-frame invariance, skin area collapse proxy')
