import json,sys,numpy as np
from PIL import Image
import os;S=os.environ.get('LAB_OUT','.')  # run from the mod's artifacts folder (%LOCALAPPDATA%/DishonoredVR/artifacts: world-ui-N.rgba)
def sm(e0,e1,x): t=np.clip((x-e0)/(e1-e0),0,1);return t*t*(3-2*t)
def load(i):
    j=json.load(open(f'world-ui-{i}.json'));w,h=j['width'],j['height']
    return np.fromfile(f'world-ui-{i}.rgba',np.uint8)[:w*h*4].reshape(h,w,4).astype(np.float32)/255
def current(c):
    h,w=c.shape[:2];y,x=np.mgrid[0:h,0:w];u=(x+.5)/w;v=(y+.5)/h
    edge=sm(.85,1.38,np.hypot((u-.5)*2,(v-.5)*2))
    bgA=.502+.498*edge;a=np.clip((c[...,3]-bgA)/np.maximum(1-bgA,1e-3),0,1)
    rgb=np.minimum(np.maximum(c[...,:3]-np.array([.046,.050,.056])*(1-.4*edge)[...,None]*(1-a)[...,None],0),a[...,None])
    return rgb,a
def keyed(c,lo=.05,hi=.14):
    rgb,a=current(c)
    lum=rgb.max(-1)/np.maximum(a,1e-3)          # the un-composited art colour: backdrop leftovers are near black
    k=sm(lo,hi,lum);return rgb*k[...,None],a*k
def over(rgb,a,bg):  # premultiplied over a bright test background
    return np.clip(rgb+bg*(1-a)[...,None],0,1)
i=int(sys.argv[1]) if len(sys.argv)>1 else 1
c=load(i);bg=np.array([.75,.55,.35],np.float32)
for name,f in [('cur',current),('key',keyed)]:
    rgb,a=f(c);img=over(rgb,a,bg)
    Image.fromarray((img*255).astype(np.uint8)).resize((c.shape[1]//3,c.shape[0]//3)).save(S+rf'\unback-{i}-{name}.png')
    # residue: dark pixels with alpha in the outer 15% frame
    h,w=a.shape;m=np.zeros_like(a,bool);m[:int(h*.15)]=1;m[-int(h*.15):]=1;m[:,:int(w*.15)]=1;m[:,-int(w*.15):]=1
    lum=rgb.max(-1)/np.maximum(a,1e-3)
    print(name,'frame mean alpha %.3f'%a[m].mean(),'dark-residue px %d'%((a>.1)&(lum<.05)&m).sum(),'centre ring alpha mean %.3f'%a[int(h*.35):int(h*.65),int(w*.3):int(w*.7)].mean())
def edged(c,lo=.05,hi=.14):
    rgb,a=current(c);h,w=a.shape;y,x=np.mgrid[0:h,0:w];u=np.abs((x+.5)/w-.5)*2;v=np.abs((y+.5)/h-.5)*2
    r4=(u**4+v**4)**.25                          # a rounded rectangle, the shape of the game's own fade to the edges
    wgt=sm(.50,.80,r4)
    lum=rgb.max(-1)/np.maximum(a,1e-3);k=1-wgt*(1-sm(lo,hi,lum))
    return rgb*k[...,None],a*k
for i in (1,2,3):
    c=load(i);rgb,a=edged(c);img=over(rgb,a,bg)
    Image.fromarray((img*255).astype(np.uint8)).resize((c.shape[1]//3,c.shape[0]//3)).save(S+rf'\unback-{i}-edge.png')
    h,w=a.shape;m=np.zeros_like(a,bool);m[:int(h*.15)]=1;m[-int(h*.15):]=1;m[:,:int(w*.15)]=1;m[:,-int(w*.15):]=1
    lum=rgb.max(-1)/np.maximum(a,1e-3);r0,a0=current(c)
    print(i,'edge frame residue px',((a>.1)&(lum<.05)&m).sum(),'centre alpha %.3f (cur %.3f)'%(a[int(h*.35):int(h*.65),int(w*.3):int(w*.7)].mean(),a0[int(h*.35):int(h*.65),int(w*.3):int(w*.7)].mean()))
