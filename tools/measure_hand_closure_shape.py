"""Offline fist silhouette measurements from full distal and palm skin vertices."""
import argparse,json
from pathlib import Path
import numpy as np
from export_native_hand_poses import verified_arm
from author_native_hand_candidate import matrices,skin
from analyse_arm_mesh import decode_mesh
from render_body_rig import palettes,skin_positions

def measure(arm,pose,asset):
 names=arm['joint_names'];side=arm['side'];source=matrices(arm);wrist=names.index(side+'_wrist_jnt');origin=source[wrist][:3,3];bases=np.array([source[names.index(side+'_'+d+'_02_jnt')][:3,3]for d in ('index','medium','ring','pinky')]);forward=bases.mean(axis=0)-origin;forward/=np.linalg.norm(forward);width=bases[0]-bases[-1];width-=forward*np.dot(forward,width);width/=np.linalg.norm(width);palette=skin(arm,pose);original,_=palettes(arm);palm=[];index_surface=[];pads={d:[]for d in ('index','medium','ring','pinky','thumb')};xp=(bases-origin)@width;yp=(bases-origin)@forward;order=np.argsort(xp)
 for surface in decode_mesh(asset.read_bytes(),69)['surfaces']:
  if not any(m in surface['material']for m in ('player_emily_hands.material','player_emily_gloves.material')):continue
  vertices=surface['vertices'];before=skin_positions(vertices,original);after=skin_positions(vertices,palette)
  for i,v in enumerate(vertices):
   point=before[i]-origin;x=np.dot(point,width);y=np.dot(point,forward);boundary=np.interp(x,xp[order],yp[order]);distal=sum(w for w,j in zip(v[1],v[2])if any(s in names[j]for s in ('_03_','_04_','_end_')))
   if .005<y<boundary-.003 and xp.min()-.025<x<xp.max()+.025 and distal<64:palm.append(after[i])
   if sum(w for w,j in zip(v[1],v[2])if 'index'in names[j])>=190:index_surface.append(after[i])
   for d in pads:
    terminal='_03_jnt'if d=='thumb'else'_04_jnt';weight=sum(w for w,j in zip(v[1],v[2])if names[j]==side+'_'+d+terminal);end=names.index(side+'_'+d+'_end_jnt')
    if weight>=190 and np.linalg.norm(before[i]-source[end][:3,3])<.025:pads[d].append(after[i])
 if len(palm)<20 or any(len(pads[d])<5 for d in pads):raise ValueError('Native palm or distal pad sample insufficient')
 palm=np.array(palm);result={}
 for d,points in pads.items():
  points=np.array(points);dist=np.linalg.norm(points[:,None,:]-palm[None,:,:],axis=2).min(axis=1)
  result[d]=dict(pad_vertices=len(points),minimum_pad_to_palm_vertex_distance_native_model_units=float(dist.min()),median_pad_to_palm_vertex_distance_native_model_units=float(np.median(dist)))
 # A nearest-vertex proxy is not a signed surface-contact certificate.
 thumb_index=float(np.linalg.norm(np.array(pads['thumb'])[:,None,:]-np.array(index_surface)[None,:,:],axis=2).min())
 return dict(palm_vertices=len(palm),pad_to_palm_proxies=result,thumb_pad_to_index_vertex_distance_native_model_units=thumb_index,contact_certified=False,silhouette_acceptance=False)

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
 if args.output.exists():raise ValueError('Refuse overwrite')
 candidate=json.loads(args.candidate.read_text());report=json.loads(Path(candidate['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';out=[]
 for item in candidate['arms']:
  arm=verified_arm(report,item['side']);out.append(dict(side=item['side'],**measure(arm,[np.asarray(m)for m in item['authored_transform_matrices']],root/(item['side']+'-arm.bmd6model'))))
 args.output.write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8');print(json.dumps(out))
if __name__=='__main__':main()
