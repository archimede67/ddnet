"""Install committed pre-v2/v2 timing probes in a disposable source checkout.

Run once on an isolated source tree matching the chosen history version. Probe
assets are bundled beside this script; no previous build or results are needed.
This script modifies --source in place and writes matched-profile-manifest.json.
Requires Python 3.10 or newer.
"""

from pathlib import Path
import argparse
import hashlib
import json
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--legacy', action='store_true')
args = parser.parse_args()
source = args.source.resolve()
editor = source / 'src/game/editor'
assets = Path(__file__).resolve().parent / ('v1' if args.legacy else 'v2')
for name in ('perf_v1_probe.h', 'perf_v1_probe.inc'):
    shutil.copyfile(assets / name, editor / name)
if args.legacy:
    factory = editor / 'editor.cpp'
    text = factory.read_text(encoding='utf-8')
    anchor = 'IEditor *CreateEditor() { return new CEditor; }'
    if anchor not in text:
        raise RuntimeError('Expected the uninstrumented pre-v2 editor factory')
    factory.write_text('#include "perf_v1_probe.h"\n' + text.replace(anchor, '#include "perf_v1_probe.inc"', 1), encoding='utf-8', newline='\n')
else:
    factory = editor / 'editor_factory.cpp'
    shutil.copyfile(assets / factory.name, factory)
io = editor / 'mapitems/map_io.cpp'
text = io.read_text(encoding='utf-8')
anchor = 'bool CEditorMap::Load(const char *pFilename, int StorageType, const FErrorHandler &ErrorHandler)\n{'
if anchor not in text or 'perf_v1::Timer PerfLoad' in text:
    raise RuntimeError('Expected an uninstrumented CEditorMap::Load')
# First local: the timer destructor includes every later local's destruction.
text = '#include <game/editor/perf_v1_probe.h>\n' + text.replace(anchor, anchor + '\n\tperf_v1::Timer PerfLoad("map_load_total");', 1)
io.write_text(text, encoding='utf-8', newline='\n')
files = [factory, editor / 'perf_v1_probe.h', editor / 'perf_v1_probe.inc', io]
(source / 'matched-profile-manifest.json').write_text(json.dumps({str(path.relative_to(editor)).replace('\\', '/'): hashlib.sha256(path.read_bytes()).hexdigest() for path in files}, indent=2) + '\n', encoding='utf-8')
print(source)
