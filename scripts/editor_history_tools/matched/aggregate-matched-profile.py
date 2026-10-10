from pathlib import Path
import argparse
import collections
import csv
import json
import math
import statistics

parser = argparse.ArgumentParser(description='Pool every measured sample from the supplied matched-run metadata; discard only the first five warmups in each run.')
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--metadata', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if args.output.exists():
    parser.error('--output must be a new directory')
metadata = json.loads(args.metadata.read_text(encoding='utf-8'))
groups = collections.defaultdict(list)
runs = []
for entry in metadata:
    result = json.loads((args.root / entry['name'] / 'results.json').read_text(encoding='utf-8'))
    if result['exit_code'] != 0 or not result['pass_marker']:
        raise RuntimeError('Unsuccessful run: ' + entry['name'])
    version, config, variant = entry['version'], entry.get('config', 'optimized'), entry['variant']
    view = 'panel50' if entry['history_panel'] else 'default'
    report = {key: value for key, value in result.items() if key != 'samples'}
    report.update(entry)
    report['load_plus_first_frame_ms'] = sum(phase['ms'] for phase in result['phases'])
    runs.append(report)
    for sample in result['samples']:
        if sample['sample'] < 5:
            continue
        sample = dict(sample)
        sample['history_begin_plus_commit'] = sample.get('edit_begin', 0) + sample['edit_commit']
        groups[version, config, variant, view, sample['label']].append(sample)
rows = []
for key, samples in sorted(groups.items()):
    for phase in samples[0]:
        if phase in ('sample', 'label'):
            continue
        values = sorted(sample[phase] for sample in samples)
        row = dict(zip(('version', 'config', 'variant', 'view', 'layer'), key))
        row.update(phase=phase, n=len(values), median_ms=statistics.median(values), p95_ms=values[math.ceil(.95 * len(values)) - 1], maximum_ms=max(values))
        rows.append(row)
args.output.mkdir(parents=True)
with (args.output / 'distributions.csv').open('w', encoding='utf-8', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
(args.output / 'runs.json').write_text(json.dumps(runs, indent=2) + '\n', encoding='utf-8')
print('Pooled', len(runs), 'runs into', len(rows), 'distributions')
