"""Validate offline contact math with known separated/crossing/shared-seam triangles."""
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from check_authored_hand_contacts import crossing,evaluate
a=np.array([[-1.,-1.,0.],[1.,-1.,0.],[0.,1.,0.]])
b=np.array([[-.2,0.,-1.],[.2,0.,1.],[0.,.5,1.]])
assert crossing(a[None],b[None])[0]
apart=b.copy();apart[:,2]+=3.
assert not crossing(a[None],apart[None])[0]
ids=[dict(surface=0,triangle=0,region='index'),dict(surface=0,triangle=1,region='thumb')]
result=evaluate(np.array([a,apart]),np.array([a,b]),ids)
assert result['introduced_triangle_crossings']==1 and result['introduced_region_pairs']=={'index/thumb':1}
assert result['triangle_plane_overlap_extent_proxy']['index/thumb']['maximum_native_units']>0
result=evaluate(np.array([a,b]),np.array([a,b]),ids)
assert result['introduced_triangle_crossings']==0 and result['existing_crossings_retained']==1
shared=b.copy();shared[0]=a[0]
result=evaluate(np.array([a,shared]),np.array([a,shared]),ids)
assert result['exact_candidate_pairs_checked']==0
print('PASS: exact triangle crossing/separation, existing-vs-introduced, overlap proxy and native shared-seam exclusion')
