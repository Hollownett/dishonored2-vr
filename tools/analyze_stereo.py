"""Check captured game-eye geometry against the poses/FOV submitted to OpenXR."""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np


def camera(meta):
    width, height = meta['width'], meta['height']
    left, right, up, down = np.tan(meta['submitted_fov'])
    if not (left < right and down < up):
        raise ValueError('Invalid eye frustum')
    intrinsic = np.array([[width/(right-left), 0, -left*width/(right-left)-.5],
                          [0, height/(up-down), up*height/(up-down)-.5], [0, 0, 1.]])
    x, y, z, w = meta['submitted_orientation']
    if abs(x*x+y*y+z*z+w*w-1) > .01:
        raise ValueError('Invalid submitted orientation')
    rotation = np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                         [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                         [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])
    # Pixel camera: right/down/forward. OpenXR: right/up/backward.
    return intrinsic, rotation @ np.diag([1., -1., -1.]), np.array(meta['submitted_position'])


def fundamental(left, right):
    kl, rl, pl = camera(left)
    kr, rr, pr = camera(right)
    t = rr.T @ (pl-pr)
    x, y, z = t
    cross = np.array([[0., -z, y], [z, 0., -x], [-y, x, 0.]])
    return np.linalg.inv(kr).T @ cross @ rr.T @ rl @ np.linalg.inv(kl)


def errors(matrix, left, right):
    a = np.column_stack((left, np.ones(len(left))))
    b = np.column_stack((right, np.ones(len(right))))
    lines = a @ matrix.T
    denominator = np.linalg.norm(lines[:, :2], axis=1)
    if np.any(denominator < 1e-14):
        raise ValueError('Degenerate stereo baseline')
    return np.abs(np.sum(lines*b, axis=1))/denominator


def depths(left_meta, right_meta, left, right):
    projections, cameras = [], []
    for m in (left_meta, right_meta):
        k, r, p = camera(m)
        projections.append(k @ np.column_stack((r.T, -r.T @ p)))
        cameras.append((r, p))
    homogeneous = cv2.triangulatePoints(*projections, left.T, right.T)
    valid = np.abs(homogeneous[3]) > 1e-10
    points = (homogeneous[:3, valid]/homogeneous[3, valid]).T
    return [(points-p) @ r[:, 2] for r, p in cameras]


def self_test():
    meta = dict(width=2496, height=2688, submitted_orientation=[0., 0., 0., 1.],
                submitted_position=[-.032, 0., 0.], submitted_fov=[-.9424778, .6981317, .7679449, -.9599311])
    right = dict(meta, submitted_position=[.032, 0., 0.], submitted_fov=[-.6981317, .9424778, .7679449, -.9599311])
    rng = np.random.default_rng(19)
    world = rng.uniform([-1., -.8, -10.], [1., .8, -1.], (200, 3))

    def project(m):
        k, r, p = camera(m)
        points = (world-p) @ r @ k.T
        return points[:, :2]/points[:, 2:]

    a, b = project(meta), project(right)
    assert np.max(errors(fundamental(meta, right), a, b)) < 1e-8
    reconstructed = depths(meta, right, a, b)
    assert np.max(np.abs(reconstructed[0]+world[:, 2])) < 1e-8
    # An incorrect vertical projection must fail even when both images look sharp.
    bad = dict(right, submitted_fov=[-.6981317, .9424778, .9, -.9599311])
    assert np.median(errors(fundamental(meta, bad), a, b)) > 20
    kl, _, _ = camera(meta)
    kr, _, _ = camera(right)
    rays_l = np.column_stack((a, np.ones(len(a)))) @ np.linalg.inv(kl).T
    rays_r = np.column_stack((b, np.ones(len(b)))) @ np.linalg.inv(kr).T
    assert np.all(rays_l[:, 0] > rays_r[:, 0])
    print('PASS: asymmetric optical frusta, pixel centres, stereo baseline and wrong-projection rejection')


def analyze(folder):
    workspace = Path(__file__).resolve().parents[1]
    folder = folder.resolve()
    if not folder.is_relative_to(workspace):
        raise ValueError('Use a workspace capture directory')
    metas, images = [], []
    for side in ('left', 'right'):
        m = json.loads((folder/f'xr-transfer-{side}.json').read_text())
        width, height = m['width'], m['height']
        if (type(width) is not int or type(height) is not int or not 1 <= width <= 4096 or
                not 1 <= height <= 4096 or not m.get('pixels_equal') or not m.get('tracked_render_pose')):
            raise ValueError('Require bounded, pixel-matched, tracked game-eye captures')
        data = (folder/f'xr-destination-{side}.rgba').read_bytes()
        if len(data) != width*height*4:
            raise ValueError('Eye image size differs from metadata')
        pixels = np.frombuffer(data, np.uint8).reshape(height, width, 4)
        if not cv2.imwrite(str(folder/f'xr-destination-{side}.png'), cv2.cvtColor(pixels, cv2.COLOR_RGBA2BGR)):
            raise ValueError('Cannot save the inspected eye pixels')
        images.append(cv2.cvtColor(pixels, cv2.COLOR_RGBA2GRAY))
        metas.append(m)
    if any(metas[0][k] != metas[1][k] for k in ('pid', 'present', 'width', 'height')):
        raise ValueError('Eye captures are not one matched native frame')
    features = cv2.SIFT_create(nfeatures=2500)
    pairs = [features.detectAndCompute(cv2.resize(im, None, fx=.5, fy=.5), None) for im in images]
    if any(d is None for _, d in pairs):
        raise ValueError('Captured scene has insufficient texture')
    matcher = cv2.BFMatcher()
    forward = matcher.knnMatch(pairs[0][1], pairs[1][1], k=2)
    backward = matcher.knnMatch(pairs[1][1], pairs[0][1], k=2)
    reverse = {pair[0].queryIdx: pair[0].trainIdx for pair in backward
               if len(pair)==2 and pair[0].distance < .72*pair[1].distance}
    matches = [pair[0] for pair in forward if len(pair)==2 and pair[0].distance < .72*pair[1].distance
               and reverse.get(pair[0].trainIdx) == pair[0].queryIdx]
    if len(matches) < 30:
        raise ValueError('Need at least 30 mutual scene matches for a geometry estimate')
    # Resize pixel centres map to the original image as (p+.5)/scale-.5.
    a = np.array([np.array(pairs[0][0][m.queryIdx].pt)*2+.5 for m in matches])
    b = np.array([np.array(pairs[1][0][m.trainIdx].pt)*2+.5 for m in matches])
    try:
        fitted, mask = cv2.findFundamentalMat(a, b, cv2.FM_RANSAC, 3., .999)
    except cv2.error as error:
        raise ValueError('Captured features are degenerate; stereo geometry is inconclusive') from error
    if fitted is None or mask is None:
        raise ValueError('Scene matching did not produce a stable stereo model')
    inliers = mask.ravel().astype(bool)
    if np.sum(inliers) < 30:
        raise ValueError('Too few geometric inliers')
    expected = fundamental(*metas)
    error = errors(expected, a[inliers], b[inliers])
    left_depth, right_depth = depths(*metas, a[inliers], b[inliers])
    positive = (left_depth > 0) & (right_depth > 0)
    positive_depths = left_depth[positive]
    report = dict(pid=metas[0]['pid'], present=metas[0]['present'], mutual_matches=len(matches),
                  geometric_inliers=int(np.sum(inliers)),
                  expected_epipolar_error_pixels_median=float(np.median(error)),
                  expected_epipolar_error_pixels_p90=float(np.percentile(error, 90)),
                  expected_epipolar_error_pixels_max=float(np.max(error)),
                  triangulated_positive_depth_fraction=float(np.mean(positive)),
                  triangulated_depth_metres_p10_p50_p90=(np.percentile(positive_depths, [10, 50, 90]).tolist()
                                                        if len(positive_depths) else None),
                  headset_comfort_verified=False,
                  limitation='Estimated depths assume submitted calibration and cannot prove its horizontal accuracy without a measured scene. This does not verify temporal effects, HUD disparity or comfort.')
    destination = folder/'stereo-geometry.json'
    destination.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--run', type=Path)
    group.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    self_test() if args.self_test else analyze(args.run)
