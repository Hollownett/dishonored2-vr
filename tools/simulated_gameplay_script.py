"""Strict developer command-script validation; commands are not gameplay outcomes."""
import json
import math
from pathlib import Path

AXES=('move_x','move_y','turn_x','turn_y','left_trigger','right_trigger','left_grip','right_grip')
BUTTONS=('a','b','x','y','left_click','right_click','menu')


def validate_script_profile(args):
    if not args.simulated_gameplay_script:
        return
    if not (args.simulated_vr and args.simulated_input and args.paired and args.native_eye_capture and
            args.vr_preview and args.head_tracking and args.controllers and args.dll_name):
        raise ValueError('Gameplay script requires timed --simulated-vr --simulated-input, paired native-eye XR, head/controllers and --dll-name')
    if args.until_stopped or not 10<=args.seconds<=120 or args.simulated_cleanup_exception or args.saved_settings:
        raise ValueError('Gameplay script requires 10..120 seconds without cleanup exception or saved settings')


def compile_script(path,root,seconds):
    path=Path(path).resolve();root=Path(root).resolve()
    if not path.is_relative_to(root) or path.suffix.lower()!='.json' or not path.is_file() or path.stat().st_size>65536:
        raise ValueError('Gameplay script must be a workspace JSON file of at most64 KiB')
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError('Duplicate script key: '+key)
            result[key]=value
        return result
    text=path.read_text(encoding='utf-8-sig')
    data=json.loads(text,object_pairs_hook=unique)
    if not isinstance(data,dict) or set(data)!={'segments'} or not isinstance(data['segments'],list) or not 1<=len(data['segments'])<=128:
        raise ValueError('Script requires only segments, with1..128 entries')
    POSES=('head_offset','left_hand_offset','right_hand_offset')
    version=4 if any(isinstance(s,dict) and 'hand_pitch_degrees' in s for s in data['segments']) else 3 if any(isinstance(s,dict) and set(POSES)&set(s) for s in data['segments']) else 2 if any(isinstance(s,dict) and 'head_pitch_degrees' in s for s in data['segments']) else 1
    duration=seconds*1000;lines=[f'D2VR_GAMEPLAY_SCRIPT {version} {duration} {len(data["segments"])}'];previous=0
    for segment in data['segments']:
        if not isinstance(segment,dict) or not {'start_ms','end_ms','input'}<=set(segment) or set(segment)-{'start_ms','end_ms','input','head_pitch_degrees','head_offset','left_hand_offset','right_hand_offset','hand_pitch_degrees'}:
            raise ValueError('Each segment requires start_ms, end_ms, input and optional head_pitch_degrees')
        pitch=segment.get('head_pitch_degrees',0)
        if type(pitch) not in (int,float) or not -80<=pitch<=80 or not math.isfinite(pitch):
            raise ValueError('Head pitch requires finite degrees in[-80,80]')
        start,end=segment['start_ms'],segment['end_ms'];values=segment['input']
        if type(start)!=int or type(end)!=int or not previous<=start<end<=duration:
            raise ValueError('Segments must be sorted, non-overlapping integer milliseconds within session')
        if not isinstance(values,dict) or not values or set(values)-set(AXES+BUTTONS):
            raise ValueError('Input requires known controller fields only')
        for name,value in values.items():
            if name in BUTTONS:
                if type(value)!=bool:raise ValueError('Button fields must be booleans')
            elif type(value) not in (int,float) or not (-1 if name in AXES[:4] else 0)<=value<=1 or not math.isfinite(value):
                raise ValueError('Analog fields require finite stick[-1,1] or trigger/grip[0,1]')
        fields=[start,end]+[values.get(name,0) for name in AXES]+[int(values.get(name,False)) for name in BUTTONS]
        if version>=2:fields.append(pitch)
        if version>=3:
            for name in POSES:
                offset=segment.get(name,[0,0,0])
                if not isinstance(offset,list) or len(offset)!=3 or any(type(v) not in (int,float) or not math.isfinite(v) or abs(v)>2 for v in offset):
                    raise ValueError(name+' requires three finite metres within 2')
                fields.extend(offset)
        if version==4:  # both hands pitched about their right axis (negative points down), degrees
            hand_pitch=segment.get('hand_pitch_degrees',0)
            if type(hand_pitch) not in (int,float) or not -89<=hand_pitch<=89 or not math.isfinite(hand_pitch):
                raise ValueError('Hand pitch requires finite degrees in[-89,89]')
            fields.append(hand_pitch)
        lines.append(' '.join(map(str,fields)));previous=end
    return text,'\n'.join(lines)+'\n'
