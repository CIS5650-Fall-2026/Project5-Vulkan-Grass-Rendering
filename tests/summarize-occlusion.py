import csv, json, statistics
from pathlib import Path
root = Path(__file__).resolve().parents[1] / 'performance/occlusion'
runs = json.loads((root/'runs.json').read_text(encoding='utf-8-sig'))
results=[]
for view in ('close','overview','distant'):
    for mode in ('off','on'):
        rows=[]; visible=[]
        for run in runs:
            if run['view'] != view or run['occlusion'] != mode: continue
            samples=list(csv.DictReader((root/run['file']).open()))
            if len(samples) != run['samples']: raise RuntimeError('Wrong sample count')
            if any(int(row['occlusion']) != (mode == 'on') for row in samples): raise RuntimeError('Wrong mode')
            rows.extend(samples); visible.append(run['final_visible'])
        result={'view':view,'occlusion':mode,'samples':len(rows),'visible':visible}
        for metric in ('compute_ms','graphics_ms','gpu_ms'):
            values=sorted(float(row['compute_ms'])+float(row['graphics_ms']) if metric=='gpu_ms' else float(row[metric]) for row in rows)
            result[metric]={'median':statistics.median(values),'mean':statistics.mean(values),'p95':values[int(.95*(len(values)-1))]}
        results.append(result)
        print(view,mode,visible,'compute',round(result['compute_ms']['median'],3),'graphics',round(result['graphics_ms']['median'],3),'total',round(result['gpu_ms']['median'],3))
(root/'summary.json').write_text(json.dumps({'blades':65536,'other_culling':'none','lod':True,'results':results},indent=2)+'\n')
