"""Offline exact-float32 world-base comparisons keyed by native receipts."""
import argparse
import bisect
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import struct

def read_rows(path):
    with path.open(newline='') as file:
        return list(csv.DictReader(file))

def base(row, prefix):
    return struct.pack('<15f', *(float(row[prefix+str(i)]) for i in range(15)))

def key(row):
    return tuple(row[field] for field in ('side','animation','model','receipt_serial','native_frame'))

def truth(value):
    return value in ('true','1')

def analyze(run, script_start=None):
    rows = read_rows(run/'native-weapon-model-base.csv')
    uploads = read_rows(run/'native-arm-upload.csv')
    if len(rows)>8192 or len(uploads)>200:
        raise ValueError('Archive bounds exceeded')
    groups = defaultdict(dict)
    for row in rows:
        group = groups[key(row)+(row['binding'],row['binding_generation'])]
        site = int(row['site'])
        if site in group:
            raise ValueError('Ambiguous site/native receipt')
        group[site] = row
        if truth(row['source_matched']) and truth(row['model_valid']) and truth(row['receipt_model_valid']):
            if row['native_frame']!=row['receipt_frame']:
                raise ValueError('Source matched across different native frames')
            if truth(row['model_matched']) != (base(row,'model_base')==base(row,'receipt_model_base')):
                raise ValueError('Runtime model equality differs from reconstructed full60bytes')
    leaves = sorted((group[1] for group in groups.values() if 1 in group), key=lambda row:int(row['native_frame']))
    index = defaultdict(list)
    for leaf in leaves:
        index[key(leaf)].append(leaf)
    triples = [group for group in groups.values() if {1,3,4}<=group.keys()]
    previous = {}; previous_equal=previous_compared=0
    dimensions=Counter(); maxima=[0.0]*15
    for leaf in leaves:
        owner=tuple(leaf[field] for field in ('side','animation','animation_generation','model','body','body_generation'))
        prior=previous.get(owner)
        if prior and int(leaf['native_frame'])==int(prior['native_frame'])+1:
            previous_compared+=1
            previous_equal+=base(leaf,'receipt_model_base')==base(prior,'model_base')
        previous[owner]=leaf
        current=struct.unpack('<15f',base(leaf,'model_base'));receipt=struct.unpack('<15f',base(leaf,'receipt_model_base'))
        for i,(a,b) in enumerate(zip(current,receipt)):
            if struct.pack('<f',a)!=struct.pack('<f',b):dimensions[i]+=1
            maxima[i]=max(maxima[i],abs(a-b))
    binary=(run/'native-arm-palettes.bin').read_bytes()
    gpu_joins=[]
    buffered_gpu_joins=[]
    for row in rows:
        if row['site']!='5':continue
        if truth(row.get('input_model_valid','0')):
            equal=truth(row['model_valid']) and base(row,'input_model_base')==base(row,'model_base')
            if truth(row['input_model_matched'])!=equal:
                raise ValueError('GPU input snapshot equality differs from full60bytes')
        if not truth(row['source_matched']):continue
        candidates=index[key(row)]
        if len(candidates)!=1:continue
        leaf=candidates[0]
        buffered_gpu_joins.append(dict(frame=int(row['native_frame']),
            current_equal=base(row,'model_base')==base(leaf,'model_base'),
            input_equal=truth(row.get('input_model_valid','0')) and base(row,'input_model_base')==base(leaf,'model_base'),
            caller=int(row['caller_rva'])))
    for upload in uploads:
        if not truth(upload['receipt_matched']):continue
        offset=int(upload['palette_offset'])+69*64
        if binary[offset:offset+60]!=base(upload,'model_base'):
            raise ValueError('Upload CSV60bytes differs from archived native model base')
        candidates=index[key(upload)]
        if len(candidates)!=1:continue
        leaf=candidates[0]
        gpu_joins.append(dict(frame=int(upload['native_frame']),
            current_equal=base(upload,'model_base')==base(leaf,'model_base'),
            receipt_equal=base(upload,'receipt_model_base')==base(leaf,'receipt_model_base')))
    modes=defaultdict(lambda:Counter())
    if script_start is not None:
        commands=read_rows(run/'simulated-gameplay-commands.csv')
        times=[int(row['elapsed_ms']) for row in commands]
        for leaf in leaves:
            elapsed=int(leaf['tick'])-script_start;at=bisect.bisect_right(times,elapsed)-1
            if at<0:continue
            command=commands[at]
            mode=('turn_command' if float(command['turn_x']) or float(command['turn_y']) else
                  'move_command' if float(command['move_x']) or float(command['move_y']) else
                  'stationary_grip_command' if float(command['right_grip']) else 'neutral_command')
            modes[mode]['frames']+=1
            modes[mode]['receipt_current_equal']+=base(leaf,'receipt_model_base')==base(leaf,'model_base')
    names=['native-weapon-model-base.csv','native-arm-upload.csv','native-arm-palettes.bin']
    return dict(run=str(run),rows=len(rows),sites=dict(Counter(row['site'] for row in rows)),
        source_qualified=sum(truth(row['source_matched']) for row in rows),complete_triplets=len(triples),
        entry_leaf_exit_current_equal=sum(len({base(group[site],'model_base') for site in (1,3,4)})==1 for group in triples),
        leaf_receipt_current_equal=sum(base(row,'model_base')==base(row,'receipt_model_base') for row in leaves),
        consecutive_frames_compared=previous_compared,receipt_equals_previous_leaf=previous_equal,
        changed_components=dict(dimensions),maximum_component_delta=maxima,
        strict_gpu_leaf_joins=len(gpu_joins),gpu_current_equals_leaf=sum(row['current_equal'] for row in gpu_joins),
        gpu_receipt_equals_leaf_receipt=sum(row['receipt_equal'] for row in gpu_joins),
        buffered_gpu_leaf_joins=len(buffered_gpu_joins),
        buffered_gpu_current_equals_leaf=sum(row['current_equal'] for row in buffered_gpu_joins),
        buffered_gpu_input_equals_leaf=sum(row['input_equal'] for row in buffered_gpu_joins),
        buffered_gpu_callers=dict(Counter(row['caller'] for row in buffered_gpu_joins)),
        tracked_qualified=sum(truth(row['tracking_matched']) for row in rows),
        command_clock_start=script_start,command_categories={name:dict(count) for name,count in modes.items()},
        sha256={name:hashlib.sha256((run/name).read_bytes()).hexdigest() for name in names},
        writer_enabled=False,retarget_lease_verified=False,
        limits=['Native joins use exact receipt/frame/owner keys; command categories use the separately supplied clock anchor',
                'Binary upload archive stops at100per side; buffered model observations stop at8192total rows',
                'Secondary branch absence does not establish safety or weapon alignment'])

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=Path,required=True)
    parser.add_argument('--script-start-tick',type=int)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=analyze(args.run,args.script_start_tick)
    text=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text,end='')
