"""Inspect bound geometry targets; an XR image size alone is insufficient."""
import argparse
import collections
import csv
import json
from pathlib import Path


def analyze(rows, output_size, phase=3):
    width, height = output_size
    if not all(isinstance(value, int) and 1 <= value <= 16384 for value in output_size):
        raise ValueError('Invalid requested scene size')
    groups = collections.defaultdict(list)
    bindings = collections.Counter()
    threads, contexts = set(), set()
    for row in rows:
        if int(row['phase']) != phase:
            continue
        threads.add(row['thread']);contexts.add(row['context'])
        if int(row['texture_width']) and int(row['texture_height']):
            bindings[(row['event'], int(row['texture_width']), int(row['texture_height']), int(row['format']))] += 1
        if row['event'].startswith('draw_indexed') and int(row.get('draw_id') or 0):
            groups[int(row['draw_id'])].append(row)
    candidates = collections.Counter()
    for draw in groups.values():
        depth = [row for row in draw if row['event'].endswith('_depth')]
        colour = [row for row in draw if not row['event'].endswith('_depth')]
        # Multiple simultaneous colour outputs plus depth distinguish geometry
        # from the final single-target UI/copy. Square shadow maps are excluded
        # by the actual viewport aspect; texture allocation may be padded.
        if not depth or len({row['slot'] for row in colour}) < 2:
            continue
        first = draw[0]
        viewport = (float(first['width']), float(first['height']))
        if min(viewport) <= 0 or abs((viewport[0]/viewport[1])/(width/height)-1) > .02:
            continue
        if not all(float(row['width']) == viewport[0] and float(row['height']) == viewport[1]
                   and int(row['texture_width']) >= viewport[0] and int(row['texture_height']) >= viewport[1]
                   for row in draw):
            continue
        formats = tuple(sorted(int(row['format']) for row in colour))
        candidates[(viewport, formats, int(depth[0]['format']))] += 1
    geometry = [dict(viewport=list(viewport), colour_formats=list(formats), depth_format=depth_format,
                     draw_calls=count, output_fraction=[viewport[0]/width, viewport[1]/height])
                for (viewport, formats, depth_format), count in candidates.items()]
    geometry.sort(key=lambda item: item['viewport'][0]*item['viewport'][1], reverse=True)
    verdict = 'inconclusive_no_scene_geometry'
    if geometry:
        fraction = geometry[0]['output_fraction']
        verdict = 'full_size_geometry_observed' if min(fraction) >= .98 else 'scene_below_requested_size'
    return dict(phase=phase, output_size=output_size, verdict=verdict, geometry_candidates=geometry,
                indexed_draw_groups=len(groups), observed_threads=len(threads), observed_contexts=len(contexts),
                bound_textures=[dict(event=event, size=[w,h], format=fmt, bindings=count)
                                for (event,w,h,fmt),count in bindings.most_common()],
                all_scene_passes_verified=False, headset_clarity_visually_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    settled = json.loads((args.run/'settled-render-state.json').read_text())
    eyes = [json.loads((args.run/f'xr-transfer-{side}.json').read_text()) for side in ('left','right')]
    if settled['trace_phase'] != 3 or settled['game_logic_mask'] != 2 or settled['tracked_gameplay_transfers'] < 180:
        raise ValueError('A settled tracked gameplay trace is required')
    size = settled['requested_size']
    if settled['output_size'] != size or any(
        eye['pid'] != settled['pid'] or eye['pose_epoch'] != settled['pose_epoch'] or
        not eye['gameplay'] or not eye['tracked_render_pose'] or not eye['pixels_equal'] or
        [eye['width'],eye['height']] != size for eye in eyes):
        raise ValueError('Settled native output and actual XR capture identities/sizes do not match')
    with (args.run/'dxgi-gpu-trace.csv').open(newline='') as stream:
        result = analyze(csv.DictReader(stream), size)
    result.update(pid=settled['pid'], present=settled['present'], pose_epoch=settled['pose_epoch'],
                  native_scale=settled['native_scale'])
    destination = args.output or args.run/'scene-resolution.json'
    destination.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({key: result[key] for key in ('verdict','output_size','geometry_candidates','indexed_draw_groups')}))


if __name__ == '__main__':
    main()
