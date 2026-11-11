#!/usr/bin/env python3
"""Real POCO sockets, FFmpeg, ffprobe and rsync; isolated HTTP/SSH fixtures, no Kinect."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

binary = sys.argv[1]
base = Path(tempfile.mkdtemp(prefix='brisas-session-'))
subprocess.run([binary, 'unit', str(base / 'unit')], check=True, timeout=50)
A = '33333333-3333-4333-8333-333333333333'

tools = base / 'tools'; tools.mkdir()
remote = base / 'mac'; remote.mkdir()
control = base / 'control'; control.mkdir()
real_rsync = shutil.which('rsync')
(tools / 'ssh').write_text('''#!/usr/bin/env python3
import os, sys, subprocess
from pathlib import Path
args = sys.argv[1:]
while args and args[0].startswith('-'):
    flag = args.pop(0)
    if flag in ('-o', '-p', '-l'): args.pop(0)
assert args.pop(0) == 'test-mac'
if (Path(os.environ['TEST_CONTROL']) / 'offline').exists(): sys.exit(255)
env = dict(os.environ, HOME=os.environ['TEST_REMOTE'])
sys.exit(subprocess.run(['/bin/sh', '-c', ' '.join(args)], cwd=env['HOME'], env=env).returncode)
''')
(tools / 'rsync').write_text('''#!/usr/bin/env python3
import os, sys, subprocess, json, hashlib
from pathlib import Path
control = Path(os.environ['TEST_CONTROL'])
source = Path(sys.argv[-2])
assert '--delay-updates' in sys.argv
assert all(x not in sys.argv for x in ('--inplace','--append','--delete'))
with (control / 'calls').open('a') as out: out.write(source.name + '\\n')
if source.suffix == '.mp4' and (control / 'fail-media').exists(): sys.exit(23)
if source.suffix == '.ready':
    folder = Path(os.environ['TEST_REMOTE']) / 'recordings'
    manifest = folder / (source.stem + '.json')
    assert hashlib.sha256(manifest.read_bytes()).hexdigest() == source.read_text().strip()
    for entry in json.loads(manifest.read_text())['arquivos']:
        assert (folder / entry['nome']).stat().st_size == entry['tamanhoBytes']
    if (control / 'fail-ready').exists(): sys.exit(23)
result = subprocess.run([os.environ['REAL_RSYNC']] + sys.argv[1:]).returncode
if result == 0 and source.suffix == '.ready' and (control / 'lost-ack').exists():
    (control / 'lost-ack').unlink(); sys.exit(23)
sys.exit(result)
''')
for p in tools.iterdir(): p.chmod(0o755)
env = dict(os.environ, PATH=str(tools)+':'+os.environ['PATH'], TEST_REMOTE=str(remote), TEST_CONTROL=str(control), REAL_RSYNC=real_rsync)
settings = base / 'settings.json'
root = base / 'recordings'

def run(mode='worker', seconds=4, **config):
    settings.write_text(json.dumps(config))
    r = subprocess.run([binary,mode,str(root),str(settings),str(seconds)], env=env, capture_output=True, text=True, timeout=seconds+35)
    assert r.returncode == 0, r.stdout + r.stderr
    return json.loads(r.stdout.splitlines()[-1])

def job(id): return json.loads((root / id / 'publication/automatic.job.json').read_text())
def ready(id): return remote / 'recordings' / (id+'.ready')

schema = json.loads(Path('docs/contracts/manifesto-gravacao-v1.schema.json').read_text())
def verify(id, thumbnail=True):
    path = remote / 'recordings' / (id+'.json'); m = json.loads(path.read_text())
    assert set(m) == set(schema['required']) and m['v'] == 1
    assert m['gravacaoId'] == id and m['sessaoId'] == A
    for field in ('gravacaoId','sessaoId','iniciadaEm','encerradaEm'):
        assert re.fullmatch(schema['properties'][field]['pattern'],m[field])
    assert datetime.datetime.fromisoformat(m['iniciadaEm']) < datetime.datetime.fromisoformat(m['encerradaEm'])
    assert 1 <= len(m['arquivos']) <= 32
    names = set(); thumbs = 0; videos = 0
    item = schema['properties']['arquivos']['items']
    for f in m['arquivos']:
        assert set(f) == set(item['required']) and f['nome'] not in names
        names.add(f['nome']); assert re.fullmatch(item['properties']['nome']['pattern'],f['nome'])
        assert f['nome'].startswith(id) and 0 < f['tamanhoBytes'] <= 9007199254740991
        assert type(f['tamanhoBytes']) is int and (path.parent/f['nome']).stat().st_size == f['tamanhoBytes']
        if f['tipo']=='thumbnail': thumbs += 1; assert f['nome'].endswith('.jpg')
        else: videos += 1; assert f['tipo']=='video' and f['nome'].endswith('.mp4')
    assert thumbs == int(thumbnail) and videos >= 2
    assert ready(id).read_text().strip() == hashlib.sha256(path.read_bytes()).hexdigest()
    assert path.read_bytes() == (root/id/path.name).read_bytes()
    for file in path.parent.glob(id+'*.mp4'):
        subprocess.run(['ffmpeg','-v','error','-i',str(file),'-f','null','-'], check=True, capture_output=True)
    return path.read_bytes()

# Capture closes before any automatic publication; retries survive separate processes.
(control/'offline').touch()
id = run('record',2.3)['id']
run(seconds=4); assert job(id)['state']=='failed' and not ready(id).exists()
assert job(id)['id']==id and list((root/id).glob('*.mp4'))
(control/'offline').unlink(); (control/'fail-media').touch()
run('retry'); assert job(id)['state']=='failed' and not ready(id).exists()
(control/'fail-media').unlink(); (control/'fail-ready').touch()
run('retry',6); assert job(id)['state']=='failed' and not ready(id).exists()
assert (remote/'recordings'/(id+'.json')).exists()
immutable = (root/id/(id+'.json')).read_bytes()
(control/'fail-ready').unlink(); (control/'lost-ack').touch()
run('retry',6); assert job(id)['state']=='failed' and ready(id).exists()
run('retry',3); assert job(id)['state']=='completed'
assert verify(id)==immutable
calls = (control/'calls').read_text(); run(seconds=2)
assert calls==(control/'calls').read_text() and verify(id)==immutable
print('PASS canonical manifest, real sizes/UTC, thumbnail, decoding, media/marker failure, lost ACK, restart, immutable UUID')

# Optional thumbnail, checksum collision and locally modified media.
root = base/'no-thumbnail'
id2 = run('record',.4,enabled=False,thumbnail=False)['id']
run(enabled=False,thumbnail=False)
assert job(id2)['prepared'] and not ready(id2).exists()
ready(id2).write_text('0'*64+'\n')
run('retry',thumbnail=False); assert job(id2)['state']=='failed' and 'Colisão' in job(id2)['error']
ready(id2).unlink()
media = next((root/id2).glob('*.mp4')); original = media.read_bytes(); media.write_bytes(original+b'x')
run('retry',thumbnail=False); assert job(id2)['state']=='failed' and not ready(id2).exists()
media.write_bytes(original)
run('retry',5,thumbnail=False); assert job(id2)['state']=='completed'; verify(id2,False)
print('PASS optional thumbnail, remote collision, changed local media blocks marker')

# Never promote a crash-interrupted final file merely because ffprobe can open it.
root = base/'interrupted'
id3 = run('record',.4,enabled=False)['id']
session = root/id3/'session.json'; state=json.loads(session.read_text()); state.update(state='recording',owner_pid=2147483647)
session.write_text(json.dumps(state))
run(); assert not ready(id3).exists() and (root/id3/'interrupted.json').exists()
print('PASS interrupted recording quarantined; no publication without encoder completion')
print('Artifacts:',base)
