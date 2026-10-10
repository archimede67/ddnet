from pathlib import Path
import argparse, hashlib, json, os, re, shutil, statistics, subprocess, time
import psutil

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--client',type=Path,required=True)
parser.add_argument('--fixture',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--timeout',type=int,default=600)
args=parser.parse_args()
source,client,fixture,work=(p.resolve() for p in (args.source,args.client,args.fixture,args.output))
assert not work.exists(), work
(work/'maps').mkdir(parents=True)
shutil.copy2(fixture,work/'maps/fixture.map')
(work/'storage.cfg').write_text('add_path .\nadd_path '+(source/'data').as_posix()+'\n',encoding='utf8')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
env.pop('GTEST_FILTER',None)
start=time.perf_counter()
peak_rss=peak_private=0
with (work/'stdout.log').open('wb') as out,(work/'stderr.log').open('wb') as err:
    process=subprocess.Popen([str(client),'maps/fixture.map','stdout_output_level -3','snd_enable 0','cl_save_settings 0','gfx_fullscreen 0','cl_editor 1'],cwd=work,env=env,stdout=out,stderr=err)
    observed=psutil.Process(process.pid)
    while process.poll() is None:
        if time.perf_counter()-start>args.timeout:
            process.kill();process.wait();raise TimeoutError(str(work))
        try:
            memory=observed.memory_info()
            peak_rss=max(peak_rss,memory.peak_wset)
            peak_private=max(peak_private,memory.peak_pagefile)
        except psutil.NoSuchProcess:pass
        time.sleep(.01)
text=(work/'stdout.log').read_text(encoding='utf8',errors='replace')
records=[]; phases=[]; memories=[]; current=None
for line in text.splitlines():
    if line.startswith('PERF_SAMPLE '):
        current=dict(item.split('=',1) for item in line.split()[1:]); current['sample']=int(current['sample']);records.append(current)
    elif line.startswith('PERF phase='):
        values=dict(item.split('=',1) for item in line.split()[1:]); phase,ms=values['phase'],float(values['ms'])
        if current is not None:current[phase]=ms
        else:phases.append(dict(phase=phase,ms=ms))
    elif line.startswith('PERF_MEMORY '):
        values=dict(item.split('=',1) for item in line.split()[1:]);memories.append({k:v if k=='phase' else int(v) for k,v in values.items()})
for row in records:
    row['complete_edit']=sum(row.get(k,0) for k in ['edit_begin','edit_write','edit_commit','edit_next_render'])
    row['complete_undo']=row['undo']+row['undo_next_render']
    row['complete_redo']=row['redo']+row['redo_next_render']
distributions={}
for label in sorted(set(r['label'] for r in records)):
    rows=[r for r in records if r['label']==label and r['sample']>=5]
    distributions[label]={}
    if rows:
        for name in rows[0]:
            if name in ('sample','label'):continue
            values=sorted(r[name] for r in rows)
            distributions[label][name]=dict(n=len(values),median=statistics.median(values),p95=values[__import__('math').ceil(.95*len(values))-1],max=max(values))
record=dict(elapsed_seconds=time.perf_counter()-start,exit_code=process.returncode,process_resident_peak=peak_rss,process_private_commit_peak=peak_private,client=str(client),client_sha256=hashlib.sha256(client.read_bytes()).hexdigest(),fixture=str(fixture),fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),pass_marker='PERF_PASS' in text,phases=phases,memory=memories,distributions=distributions,samples=records)
(work/'results.json').write_text(json.dumps(record,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in record.items() if k not in ('samples','distributions')},indent=2))
for label,values in distributions.items():print(label,json.dumps({k:v for k,v in values.items() if k.startswith('complete') or k in ('edit_commit','undo','redo')}))
raise SystemExit(0 if process.returncode==0 and record['pass_marker'] else 1)
