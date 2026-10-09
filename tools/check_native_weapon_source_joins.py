"""Assert archived native Sword source receipt joins; never opens a game process."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct


def check(run):
    run=Path(run)
    with (run/'native-weapon-attachment.csv').open(newline='') as file:
        attachment=list(csv.DictReader(file))
    with (run/'native-arm-upload.csv').open(newline='') as file:
        uploads=list(csv.DictReader(file))
    palettes=(run/'native-arm-palettes.bin').read_bytes()
    if not attachment or len(attachment)>8192 or len(uploads)>200:
        raise ValueError('Require nonempty bounded attachment/archive data')
    qualified={}
    for row in uploads:
        if row['receipt_matched']!='true':continue
        key=(row['receipt_serial'],row['native_frame'],row['side'])
        if key in qualified:raise ValueError('Ambiguous archived upload receipt')
        if row['receipt_reason']!='0' or row['native_frame']!=row['receipt_frame'] or row['source']!=row['receipt_source']:
            raise ValueError('Qualified upload metadata differs')
        qualified[key]=row
    joins=[]
    for row in attachment:
        if row['receipt_matched']!='true' or row['receipt_reason']!='0' or row['native_frame']!=row['receipt_frame']:
            raise ValueError('Attachment lacks exact source-frame qualification')
        if row['receipt_producer_stage']!='3' or row['leaf_count']!='1':
            raise ValueError('Attachment source stage/leaf differs')
        upload=qualified.get((row['receipt_serial'],row['native_frame'],row['side']))
        if upload is None:continue # Existing upload archive stops at100per side.
        for field in ('animation','model','receipt_game','receipt_source','receipt_remap','receipt_epoch','receipt_producer_caller_rva'):
            if row[field]!=upload[field]:raise ValueError('Source owner/key differs: '+field)
        joint=int(row['joint']);offset=int(upload['palette_offset'])+joint*64
        if not 0<=joint<69 or offset<0 or offset+64>len(palettes):raise ValueError('Palette joint/range differs')
        if int(row['source'])!=int(upload['source'])+joint*64:raise ValueError('Socket address differs')
        binary=struct.unpack_from('<16f',palettes,offset);errors=[]
        for i,value in enumerate(binary):
            observed=float(row['source_matrix'+str(i)])
            if i in (3,7,11,15) and math.isnan(value) and math.isnan(observed):continue
            if not math.isfinite(value) or not math.isfinite(observed):raise ValueError('Invalid finite source component')
            error=abs(value-observed);errors.append(error)
            if error>1e-5:raise ValueError('Socket matrix differs beyond CSV precision')
        joins.append(dict(receipt=int(row['receipt_serial']),frame=int(row['native_frame']),side=int(row['side']),
                          matrix_max_absolute_csv_error=max(errors)))
    if not joins:raise ValueError('No strict receipt/frame/side archive overlap')
    return dict(run=str(run),attachment_rows=len(attachment),qualified_uploads=len(qualified),strict_joins=len(joins),
                maximum_matrix_csv_error=max(row['matrix_max_absolute_csv_error']for row in joins),
                input_sha256={name:hashlib.sha256((run/name).read_bytes()).hexdigest()for name in
                    ('native-weapon-attachment.csv','native-arm-upload.csv','native-arm-palettes.bin')},
                game_memory_writes=False,engine_calls=False,model_world_frame_coherence_verified=False,
                tracked_pose_publication_coherence_verified=False,weapon_retarget_outcome_verified=False,
                limits=['Upload archival is capped100per side','CSV comparison cannot reproduce native SIMD NaN payload bits',
                        'Runtime qualification compares full69native bytes; this offline check compares the recorded socket'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run',type=Path,required=True);parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=check(args.run)
    if args.output:args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
