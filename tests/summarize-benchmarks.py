import csv
import json
import statistics
from pathlib import Path

root = Path(__file__).resolve().parents[1]
runs = json.loads((root / 'performance/runs.json').read_text(encoding='utf-8-sig'))
groups = {}
for run in runs:
    key = (run['view'], run['blades'], run['culling'])
    group = groups.setdefault(key, {'samples': [], 'visible': [], 'runs': 0})
    with (root / 'performance/measurements' / run['file']).open() as stream:
        samples = list(csv.DictReader(stream))
    if len(samples) != run['samples']:
        raise RuntimeError(f"Wrong sample count: {run['file']}")
    group['samples'].extend(samples)
    group['visible'].append(run['final_visible'])
    group['runs'] += 1

results = []
for (view, count, mode), group in sorted(groups.items()):
    result = {'view': view, 'blades': count, 'culling': mode, 'runs': group['runs'],
              'samples': len(group['samples']), 'final_visible': group['visible']}
    for metric in ('compute_ms', 'graphics_ms', 'frame_ms', 'gpu_ms'):
        values = [float(row['compute_ms']) + float(row['graphics_ms']) if metric == 'gpu_ms' else float(row[metric]) for row in group['samples']]
        values.sort()
        result[metric] = {'mean': statistics.mean(values), 'median': statistics.median(values),
                          'p95': values[int(0.95 * (len(values) - 1))]}
    results.append(result)
output = {'gpu': 'NVIDIA GeForce RTX 3070', 'driver': '616.56', 'build': 'Release',
          'resolution': [640, 480], 'fixed_dt': 1 / 60, 'wind_strength': 6,
          'collision': True, 'occlusion': False, 'lighting': 'sky and warm sunlight', 'date': '2026-10-25', 'results': results}
(root / 'performance/summary.json').write_text(json.dumps(output, indent=2) + '\n')
for view in ('close', 'overview'):
    print('\n' + view)
    print('| Blades | Culling | Final visible | Compute ms | Graphics ms | GPU total ms |')
    print('| ---: | --- | ---: | ---: | ---: | ---: |')
    for row in results:
        if row['view'] == view:
            print(f"| {row['blades']:,} | {row['culling']} | {row['final_visible'][0]:,} | {row['compute_ms']['median']:.3f} | {row['graphics_ms']['median']:.3f} | {row['gpu_ms']['median']:.3f} |")
