"""Compare verified native wrappers to actual IA pointers/ranges, never counts alone."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

def matches(surface,rows):
    if len(surface['native_index_buffers'])!=1:return False
    index=surface['native_index_buffers'][0];vertices=surface['native_vertex_buffers']
    if not vertices or len(rows)!=len(vertices):return False
    slots={int(row['vertex_slot']):row for row in rows}
    if len(slots)!=len(rows):return False
    for vertex in vertices:
        row=slots.get(vertex['slot'])
        if row is None or any(int(row[key])!=value for key,value in (
            ('vertex_buffer',vertex['com_buffer']),('vertex_stride',vertex['entry_stride']),
            ('vertex_offset',vertex['entry_base_offset']),('vertex_byte_width',vertex['byte_width']),
            ('index_buffer',index['com_buffer']),('index_offset',index['entry_base_offset']),
            ('index_byte_width',index['byte_width']),('index_format',57 if surface['native_index_element_size']==2 else 42),
            ('topology',4),('instances',1),('first_instance',0))):return False
        # The present exact fixtures have one full zero-offset triangle range.
        # Reject unknown native range semantics or partial draws conservatively.
        if surface['draw_range_records']!=[[0,0,surface['indices']]] or int(row['start_index'])!=0 or int(row['base_vertex'])!=0 or int(row['index_count'])!=surface['indices']:return False
    return True

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',type=Path,required=True);p.add_argument('--draws',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    native=json.loads(args.native.read_text());groups={}
    for row in csv.DictReader(args.draws.open()):groups.setdefault((row['phase'],row['context'],row['draw_id']),[]).append(row)
    result=[]
    for arm in native['arms']:
        for number,surface in enumerate(arm['geometry_layout']['surfaces']):
            found=[dict(phase=key[0],context=key[1],draw_id=key[2],vertex_shader=rows[0]['vertex_shader'],pixel_shader=rows[0]['pixel_shader'])for key,rows in groups.items()if matches(surface,rows)]
            result.append(dict(side=arm['side'],model=arm['model'],surface=surface['surface'],surface_index=number,vertices=surface['vertices'],indices=surface['indices'],
                submitted_draw_identity_matches=found,all_native_vertex_and_index_buffers_exact=bool(found),
                matched_draw_range=[0,0,surface['indices']]if found else None,
                absent_from_bounded_samples_does_not_prove_hidden=not bool(found),material_name_independently_verified=True,
                native_material_name=surface['native_material_name'],native_model_resource_name=arm['geometry_layout']['primary_resource_name'],
                pixel_visibility_or_occlusion_proven=False))
    output=dict(native_snapshot_sha256=hashlib.sha256(args.native.read_bytes()).hexdigest(),draw_csv_sha256=hashlib.sha256(args.draws.read_bytes()).hexdigest(),
        game_memory_writes=False,engine_calls=False,gpu_readback=False,poses_activated=False,surfaces=result,
        limits=['Snapshot taken after draw trace: ownership stable within snapshot, no render-frame atomicity','Submitted draw does not establish surviving pixels','CPU index bytes released after upload; no GPU readback fingerprint'])
    args.output.write_text(json.dumps(output,indent=2)+'\n');print(json.dumps([dict(side=s['side'],surface=s['surface_index'],draws=len(s['submitted_draw_identity_matches']))for s in result]))

if __name__=='__main__':main()
