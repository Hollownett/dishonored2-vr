"""Read-only export of Emily's two arm meshes into ignored research artifacts.

Container format reference: https://github.com/chipolux/disrev/blob/main/resources.py
No game files are modified; this is an independent bounded format reader.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from rig_fields import GAME_HASH

MAX_ASSET=32*1024*1024
def exact(stream,size):
    if size<0 or size>64*1024*1024: raise ValueError('Excessive resource read')
    data=stream.read(size)
    if len(data)!=size: raise ValueError('Truncated resource data')
    return data

def index_entries(stream):
    header=exact(stream,32)
    if header[:4]!=b'\x05SER': raise ValueError('Unexpected index magic')
    count=struct.unpack('>I',exact(stream,4))[0]
    if count>250000: raise ValueError('Excessive resource entry count')
    for _ in range(count):
        ident=struct.unpack('>I',exact(stream,4))[0]
        names=[]
        for _ in range(3):
            size=struct.unpack('<I',exact(stream,4))[0]
            if size>4096: raise ValueError('Excessive resource name')
            name=exact(stream,size).decode('utf-8')
            if '\0' in name: raise ValueError('Invalid resource name')
            names.append(name)
        offset,size,packed,unknown,flags1,flags2=struct.unpack('>QII6sHH',exact(stream,26))
        yield dict(id=ident,type=names[0],source=names[1],destination=names[2],offset=offset,
                   size=size,packed=packed,resource_id=flags2>>2)

def unpack_asset(packed,size):
    if not 0<size<=MAX_ASSET or not 0<len(packed)<=MAX_ASSET: raise ValueError('Excessive or empty arm asset')
    if len(packed)==size: return packed
    codec=zlib.decompressobj();data=codec.decompress(packed,size+1)
    if len(data)!=size or not codec.eof or codec.unconsumed_tail or codec.unused_data.strip(b'\0'):
        raise ValueError('Resource decompression does not match the declared bounded size')
    return data

def resource_path(base,container,ident):
    if container not in ('game1','game2','game3','game4'): raise ValueError('Unexpected container')
    if ident==8192: return base/'shared_2_3.sharedrsc'
    if not 0<=ident<=6: raise ValueError('Unknown resource file selector')
    suffix='' if ident==0 else '_patch' if ident==6 else f'_{ident:03d}'
    return base/(container+suffix+'.resources')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2'))
    args=parser.parse_args();game=args.game.resolve();workspace=Path(__file__).resolve().parents[1]
    exe=game/'Dishonored2.exe'
    with exe.open('rb') as stream:
        if hashlib.file_digest(stream,'sha256').hexdigest()!=GAME_HASH: raise ValueError('Game build differs from pinned Steam executable')
    output=workspace/'artifacts'/'research'/'native-arm-assets';output.mkdir(parents=True,exist_ok=True)
    if output.is_relative_to(game): raise ValueError('Research output must be outside the game installation')
    found={};evidence=[]
    for container in ('game1','game2','game3','game4'):
        index=game/'base'/(container+'_patch.index')
        if not index.exists(): raise ValueError('Missing final patch index; do not silently use older assets')
        if index.stat().st_size>64*1024*1024: raise ValueError('Excessive index size')
        with index.open('rb') as stream:
            for entry in index_entries(stream):
                name=entry['destination'].replace('\\','/').lower()
                source=entry['source'].replace('\\','/').lower()
                side=next((side for side in ('left','right') if source.endswith(f'/player_{side}arm_emily.md6mesh')),None)
                if not side or entry['type']!='baseModel' or not name.endswith('.bmd6model') or not entry['size'] or not entry['packed']: continue
                if entry['size']>MAX_ASSET or entry['packed']>MAX_ASSET: raise ValueError('Arm mesh exceeds bounded extraction size')
                resource=resource_path(game/'base',container,entry['resource_id'])
                if entry['offset']<4 or entry['offset']+entry['packed']>resource.stat().st_size:
                    raise ValueError('Arm resource lies outside its container')
                with resource.open('rb') as asset:
                    if exact(asset,4)!=b'\x04SER': raise ValueError('Unexpected resource magic')
                    asset.seek(entry['offset']);data=unpack_asset(exact(asset,entry['packed']),entry['size'])
                if data[:4]!=b'MM6B' or data[-4:]!=b'MM6B': raise ValueError('Native MD6 mesh magic differs')
                digest=hashlib.sha256(data).hexdigest()
                if side in found and found[side]!=digest: raise ValueError('Conflicting arm meshes across final container indexes')
                found[side]=digest;target=output/(side+'-arm.bmd6model')
                if target.exists() and target.read_bytes()!=data: raise ValueError('Preserve the prior arm asset before replacing it')
                if not target.exists(): target.write_bytes(data)
                evidence.append(dict(side=side,index=str(index),resource=str(resource),entry=entry,output=str(target),sha256=digest))
    if set(found)!= {'left','right'}: raise ValueError('Both final Emily arm meshes were not located')
    (output/'extraction.json').write_text(json.dumps(dict(executable_sha256=GAME_HASH,game_files_modified=False,
        reference='https://github.com/chipolux/disrev/blob/main/resources.py',entries=evidence),indent=2)+'\n')
    print(json.dumps(dict(output=str(output),mesh_hashes=found,game_files_modified=False)))

if __name__=='__main__': main()
