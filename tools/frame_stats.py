"""Frame-interval statistics from a run's xr-frame-timing.csv (present-to-present on the CPU).

Usage: python tools/frame_stats.py <run dir> [<run dir> ...]
"""
import csv
import sys
from pathlib import Path
import numpy as np


def stats(run):
    rows = list(csv.DictReader(open(Path(run) / 'xr-frame-timing.csv')))
    start = np.array([int(r['start_cpu_ns']) for r in rows], np.float64)
    gap = np.array([int(r['engine_cpu_gap_ns']) for r in rows], np.float64)
    submitted = np.array([r.get('submitted', '1') == '1' for r in rows])
    interval = np.diff(start)[submitted[1:]] / 1e6
    interval = interval[(interval > 0) & (interval < 200)]
    gap = gap[(gap > 0)] / 1e6
    gpu = np.array([int(r.get('native_gpu_list_sum_ns') or 0) for r in rows], np.float64) / 1e6
    gpu = gpu[gpu > 0]
    pct = lambda a, q: float(np.percentile(a, q)) if len(a) else float('nan')
    return dict(frames=len(interval), interval_med=pct(interval, 50), interval_p95=pct(interval, 95),
                fps=1000 / pct(interval, 50) if len(interval) else 0, engine_gap_med=pct(gap, 50),
                gpu_list_med=pct(gpu, 50))


if __name__ == '__main__':
    for run in sys.argv[1:]:
        s = stats(run)
        print('%-60s frames=%4d interval med %.2f ms p95 %.2f ms (%.1f fps) engine gap %.2f ms gpu lists %.2f ms'
              % (Path(run).name[-55:], s['frames'], s['interval_med'], s['interval_p95'], s['fps'], s['engine_gap_med'], s['gpu_list_med']))
