import csv
import json
import statistics
from pathlib import Path

root = Path(__file__).resolve().parents[1] / 'performance/lod'
runs = json.loads((root / 'runs.json').read_text(encoding='utf-8-sig'))
results = []
for view in ('close', 'overview', 'distant'):
    for lod in ('fixed', 'distance'):
        samples = []
        for run in runs:
            if run['view'] != view or run['lod'] != lod:
                continue
            with (root / run['file']).open() as stream:
                rows = list(csv.DictReader(stream))
            if len(rows) != run['samples'] or run['final_visible'] != 65536:
                raise RuntimeError('LOD comparison must draw the same number of blades')
            if any(int(row['lod']) != (lod == 'distance') for row in rows):
                raise RuntimeError('Wrong tessellation mode')
            samples.extend(rows)
        if len(samples) != 360:
            raise RuntimeError('Expected two runs with 180 measured frames each')
        result = {'view': view, 'lod': lod, 'samples': len(samples)}
        for metric in ('compute_ms', 'graphics_ms', 'gpu_ms'):
            values = sorted(float(row['compute_ms']) + float(row['graphics_ms']) if metric == 'gpu_ms'
                            else float(row[metric]) for row in samples)
            result[metric] = {'median': statistics.median(values), 'mean': statistics.mean(values),
                              'p95': values[int(0.95 * (len(values) - 1))]}
        results.append(result)
        print(view, lod, 'graphics', round(result['graphics_ms']['median'], 3),
              'total', round(result['gpu_ms']['median'], 3))
(root / 'summary.json').write_text(json.dumps({'blades': 65536, 'culling': 'none',
    'resolution': [640, 480], 'warmup': 60, 'results': results}, indent=2) + '\n')
