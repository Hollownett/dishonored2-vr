"""Offline hand-only native skin renders in an anatomical palm frame."""
import argparse,json
from pathlib import Path
import numpy as np
from author_native_hand_candidate import matrices,skin
from analyse_arm_mesh import decode_mesh
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions

def hand_crop_mask(before,origin,forward):
    return ((before-origin)@forward>-.025)&(np.linalg.norm(before-origin,axis=1)<.30)

def render_close(arm,pose,asset,target,label):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from mpl_toolkits.mplot3d.art3d import Poly3DCollection
    names=arm['joint_names'];side=arm['side'];source=matrices(arm);wrist=names.index(side+'_wrist_jnt');origin=source[wrist][:3,3]
    chains=[[names.index(f'{side}_{digit}_{segment}_jnt')for segment in (('01','02','03','end')if digit=='thumb'else('02','03','04','end'))]for digit in ('index','medium','ring','pinky','thumb')]
    bases=np.array([source[c[0]][:3,3]for c in chains[:4]])
    forward=bases.mean(axis=0)-origin;forward/=np.linalg.norm(forward)
    width=bases[0]-bases[-1];width-=forward*np.dot(width,forward);width/=np.linalg.norm(width)
    normal=np.cross(width,forward);frame=np.column_stack((width,forward,normal))
    original,_=palettes(arm);palette=skin(arm,pose);pieces=[]
    for surface in decode_mesh(asset.read_bytes(),69)['surfaces']:
        if not any(x in surface['material']for x in ('player_emily_hands.material','player_emily_gloves.material')):continue
        vertices=surface['vertices'];before=skin_positions(vertices,original);points=(skin_positions(vertices,palette)-origin)@frame
        triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)
        # The wrist cut excludes arm/glove cuff, using the source anatomical longitudinal coordinate.
        visible=triangles[np.all(hand_crop_mask(before,origin,forward)[triangles],axis=1)]
        if len(visible):pieces.append((points,visible))
    visible=np.concatenate([p[t].reshape(-1,3)for p,t in pieces]);lo=visible.min(axis=0);hi=visible.max(axis=0);centre=(lo+hi)/2;radius=max(hi-lo)*.54
    figure=plt.figure(figsize=(18,12));colors=['#eab839','#d55744','#42a479','#527bd6','#bc6cac']
    for col,(elev,azim,title)in enumerate(((90,-90,'Palm-normal +'),(-90,-90,'Palm-normal -'),(0,0,'Side / width axis'))):
        ax=figure.add_subplot(2,3,col+1,projection='3d')
        for points,triangles in pieces:
            faces=points[triangles];normals=np.cross(faces[:,1]-faces[:,0],faces[:,2]-faces[:,0]);normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-12)
            light=np.array([.3,.45,.84]);light/=np.linalg.norm(light);intensity=.4+.6*np.abs(normals@light)
            colors_skin=np.column_stack((np.array([.72,.64,.56])[None,:]*intensity[:,None],np.ones(len(faces))))
            ax.add_collection3d(Poly3DCollection(faces,facecolors=colors_skin,edgecolor='#554c43',linewidth=.025))
        for chain,color,digit in zip(chains,colors,('index','middle','ring','pinky','thumb')):
            joints=(np.array([pose[i][:3,3]for i in chain])-origin)@frame
            ax.plot(*joints.T,color=color,linewidth=2,marker='o',markersize=3)
            ax.text(*joints[0],digit,color=color,fontsize=8)
        for axis,color,text in zip(np.eye(3)*.035,('red','green','blue'),('palm width','palm forward','palm normal')):
            ax.quiver(0,0,0,*axis,color=color,arrow_length_ratio=.15);ax.text(*axis,text,color=color,fontsize=8)
        ax.scatter(0,0,0,color='black',s=20);ax.text(0,0,0,'wrist',fontsize=9)
        ax.set(xlim=(centre[0]-radius,centre[0]+radius),ylim=(centre[1]-radius,centre[1]+radius),zlim=(centre[2]-radius,centre[2]+radius),title=title)
        ax.set_box_aspect((1,1,1));ax.view_init(elev=elev,azim=azim);ax.set_axis_off()
        bones=figure.add_subplot(2,3,col+4,projection='3d')
        for chain,color,digit in zip(chains,colors,('index','middle','ring','pinky','thumb')):
            joints=(np.array([pose[i][:3,3]for i in chain])-origin)@frame
            bones.plot(*joints.T,color=color,linewidth=3,marker='o',markersize=5)
            for k,point in enumerate(joints):bones.text(*point,names[chain[k]].removeprefix(side+'_').removesuffix('_jnt'),color=color,fontsize=8)
        for vector,color,text in zip(np.eye(3)*.035,('red','green','blue'),('width','forward','normal')):
            bones.quiver(0,0,0,*vector,color=color);bones.text(*vector,text,color=color,fontsize=9)
        bones.text(0,0,0,'wrist',fontsize=10)
        bones.set(xlim=(centre[0]-radius,centre[0]+radius),ylim=(centre[1]-radius,centre[1]+radius),zlim=(centre[2]-radius,centre[2]+radius),title='Native joints / source palm frame')
        bones.set_box_aspect((1,1,1));bones.view_init(elev=elev,azim=azim);bones.set_axis_off()
    figure.suptitle(f'{side.capitalize()} | {label}\nNative model units; private authored candidate; no geometry/headset acceptance',fontsize=14)
    figure.tight_layout();figure.savefig(target,dpi=160);plt.close(figure)
    return dict(crop_bounds_native_model_units=[lo.tolist(),hi.tolist()],triangle_count=sum(len(t)for _,t in pieces),source_palm_frame=frame.tolist(),bone_names=[names[j]for c in chains for j in c])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    if args.output.exists():raise ValueError('Use a new output directory')
    candidate=json.loads(args.candidate.read_text());report=json.loads(Path(candidate['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';args.output.mkdir(parents=True);out=[]
    for item in candidate['arms']:
        arm=verified_arm(report,item['side'])
        for state,key in [('closed','authored_transform_matrices'),('index-released','index_released_transform_matrices')]:
            stats=render_close(arm,[np.asarray(m)for m in item[key]],root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+state+'.png'),state)
            out.append(dict(side=item['side'],state=state,**stats))
    (args.output/'render-metadata.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()


