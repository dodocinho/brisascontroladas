#!/usr/bin/env python3
"""Real FFmpeg/ffprobe/rsync; fake SSH runs the remote commands in an isolated HOME."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

base = Path(tempfile.mkdtemp(prefix='brisas-transfer-'))
tools = base / 'tools'
tools.mkdir()
remote = base / 'mac'
remote.mkdir()
control = base / 'control'
control.mkdir()
(tools / 'ssh').write_text('''#!/usr/bin/env python3
import os, sys, subprocess
from pathlib import Path
args = sys.argv[1:]
while args and args[0].startswith('-'):
    flag = args.pop(0)
    if flag in ('-o', '-p', '-l'): args.pop(0)
assert args.pop(0) == 'test-mac'
command = ' '.join(args)
control = Path(os.environ['TEST_CONTROL'])
with (control / 'calls').open('a') as out: out.write(command + '\\n')
if (control / 'offline').exists(): sys.exit(255)
if (control / 'fail-jpeg').exists() and 'rsync' in command and '.jpg' in command: sys.exit(255)
if (control / 'hang').exists():
    import time
    time.sleep(120)
commit = 'mv -f' in command
if commit:
    # Backend must not observe a marker before both payloads exist.
    import re
    id = re.search(r"([0-9a-f-]{36})\\.json", command).group(1)
    target = Path(os.environ['TEST_REMOTE']) / 'recordings'
    assert (target / (id + '.mp4')).stat().st_size > 0
    if not (control / 'no-thumbnail').exists(): assert (target / (id + '.jpg')).stat().st_size > 0
    if (control / 'fail-commit').exists(): sys.exit(255)
env = dict(os.environ, HOME=os.environ['TEST_REMOTE'])
result = subprocess.run(['/bin/sh', '-c', command], cwd=env['HOME'], env=env).returncode
if commit and (control / 'lost-ack').exists():
    (control / 'lost-ack').unlink()
    sys.exit(255)
sys.exit(result)
''')
(tools / 'ssh').chmod(0o755)
(tools / 'ffprobe').symlink_to(shutil.which('ffprobe'))
real_ffmpeg = shutil.which('ffmpeg')
(tools / 'ffmpeg').write_text(f'''#!/bin/sh
if test -e "$TEST_CONTROL/thumbnail-fail"; then exit 1; fi
exec {real_ffmpeg} "$@"
''')
(tools / 'ffmpeg').chmod(0o755)
env = dict(os.environ, PATH=str(tools) + ':' + os.environ['PATH'], TEST_CONTROL=str(control), TEST_REMOTE=str(remote), TEST_CAPTURE_FFMPEG=real_ffmpeg)
config = base / 'config.json'
root = base / 'sessions'

def run(mode='worker', seconds=3, **settings):
    config.write_text(json.dumps(settings))
    result = subprocess.run([sys.argv[1], mode, str(root), str(config), str(seconds)], env=env,
                            text=True, capture_output=True, timeout=seconds + 35)
    if result.returncode:
        print(result.stdout, result.stderr)
        raise AssertionError(result.returncode)
    return json.loads(result.stdout.splitlines()[-1])

def jobs():
    return {p: json.loads(p.read_text()) for p in root.glob('*/publication/*.job.json')}

def public():
    return list((remote / 'recordings').glob('*.json'))

def verify():
    for path in public():
        data = json.loads(path.read_text())
        assert str(uuid.UUID(data['id'])) == data['id']
        assert data['source'] == 'kinect' and data['device'] == os.uname().nodename
        assert data['startedAt'].endswith('Z') and data['endedAt'] >= data['startedAt']
        assert data['durationMs'] > 0 and data['captureDurationMs'] >= 0
        assert data['sessionId'] and data['stream'] in ('rgb', 'depth')
        assert (path.parent / data['video']).stat().st_size == data['sizeBytes']
        if data['thumbnail']:
            p = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'stream=width,height',
                                         '-of', 'json', str(path.parent / data['thumbnail'])])
            image = json.loads(p)['streams'][0]
            assert (image['width'], image['height']) == (320, 240)
        assert 'participantId' not in data

# Finished segments publish while capture is still accepting frames.
run('live', 5.3)
run('drain', 20)
assert jobs() and all(j['state'] == 'completed' for j in jobs().values())
verify()
first = {p.name: p.read_bytes() for p in public()}
assert len(first) == len(jobs())
run(seconds=2)
assert first == {p.name: p.read_bytes() for p in public()}
print('PASS: live segmentation, short thumbnails, metadata, restart without duplicate publication')

# Offline capture remains responsive, local videos survive, bounded retries persist.
(control / 'offline').touch()
run('record', 2.3)
run('exhausted', 20)
failed = {p: j for p, j in jobs().items() if j['state'] == 'failed'}
assert failed and all(j['attempts'] == 2 for j in failed.values())
ids = {p: j['id'] for p, j in failed.items()}
assert len(public()) == len(first)
run(seconds=2)
assert all(jobs()[p]['attempts'] == 2 for p in failed)
(control / 'offline').unlink()
run('retry', 20)
assert all(j['state'] == 'completed' for j in jobs().values())
assert all(jobs()[p]['id'] == id for p, id in ids.items())
verify()
print('PASS: offline failure, retry limit, persistent failed state, manual recovery with stable IDs')

# Thumbnail failure must not publish JSON; repair/restart preserves video and UUID.
(control / 'thumbnail-fail').touch()
run('record', 1.3, ffmpeg=str(tools / 'ffmpeg'))
run('exhausted', 20, ffmpeg=str(tools / 'ffmpeg'))
failed = {p: j for p, j in jobs().items() if j['state'] == 'failed'}
assert failed and all(not j['prepared'] for j in failed.values())
assert all(not (remote / 'recordings' / (j['id'] + '.json')).exists() for j in failed.values())
(control / 'thumbnail-fail').unlink()
run('retry', 20)
assert all(j['state'] == 'completed' for j in jobs().values())
print('PASS: thumbnail failure and recovery')

# Media succeeded but JSON commit failed. Restart retries the same temporary marker.
(control / 'fail-commit').touch()
run('record', 1.3, maxRetries=-1)
run('failures', 20, maxRetries=-1)
pending = {p: j for p, j in jobs().items() if j['state'] != 'completed'}
assert pending
for j in pending.values():
    assert (remote / 'recordings' / (j['id'] + '.mp4')).exists()
    assert not (remote / 'recordings' / (j['id'] + '.json')).exists()
(control / 'fail-commit').unlink()
(control / 'lost-ack').touch()
run('drain', 20, maxRetries=-1)
assert all(j['state'] == 'completed' for j in jobs().values())
assert len(public()) == len(jobs())
verify()
print('PASS: JSON published last, interrupted commit, lost SSH acknowledgment, idempotent retry')

# Disabled transfer prepares everything locally without any SSH invocation.
previous_calls = (control / 'calls').read_text()
run('record', 1.3, enabled=False)
run(seconds=2, enabled=False)
assert (control / 'calls').read_text() == previous_calls
assert any(j['prepared'] and j['state'] == 'pending' for j in jobs().values())
# Hanging SSH is killed promptly on worker shutdown, without consuming a retry.
(control / 'hang').touch()
run(seconds=1, maxRetries=-1)
(control / 'hang').unlink()
run('drain', 20)
assert all(j['state'] == 'completed' for j in jobs().values())
print('PASS: disabled transfer, bounded shutdown during stalled SSH, recovery')

# A failed JPEG transfer cannot publish the final JSON even after MP4 success.
(control / 'fail-jpeg').touch()
run('record', 1.3, maxRetries=-1)
run('failures', 20, maxRetries=-1)
pending = {p: j for p, j in jobs().items() if j['state'] != 'completed'}
assert pending
for j in pending.values():
    assert (remote / 'recordings' / (j['id'] + '.mp4')).exists()
    assert not (remote / 'recordings' / (j['id'] + '.json')).exists()
(control / 'fail-jpeg').unlink()
run('drain', 20, maxRetries=-1)
print('PASS: media transfer failure withholds publication marker')

# Explicit thumbnail opt-out still publishes truthful metadata.
(control / 'no-thumbnail').touch()
run('record', 1.3, thumbnailEnabled=False)
run('drain', 20, thumbnailEnabled=False)
assert any(json.loads(p.read_text())['thumbnail'] is None for p in public())
(control / 'no-thumbnail').unlink()
verify()
print('PASS: thumbnail disabled without fictitious JPEG reference')

# An existing different marker must block any overwrite of published media.
job_path, job = next(iter(jobs().items()))
marker = remote / 'recordings' / (job['id'] + '.json')
media = remote / 'recordings' / (job['id'] + '.mp4')
saved_marker, saved_media = marker.read_bytes(), media.read_bytes()
conflict = json.loads(saved_marker)
conflict['durationMs'] += 1
marker.write_text(json.dumps(conflict))
media.write_bytes(b'other-recording-do-not-overwrite')
job['state'] = 'pending'
job['nextAttemptMs'] = 0
job['attempts'] = 0
job_path.write_text(json.dumps(job))
run('exhausted', 20)
assert jobs()[job_path]['state'] == 'failed'
assert media.read_bytes() == b'other-recording-do-not-overwrite'
marker.write_bytes(saved_marker)
media.write_bytes(saved_media)
run('retry', 20)
print('PASS: conflicting remote marker cannot overwrite another recording')

# Stop before the initial 100ms playout buffer has elapsed: still a valid tiny video.
run('record', .001)
run('drain', 20)
assert any(json.loads(p.read_text())['durationMs'] < 100 for p in public())
verify()
print('PASS: recording shorter than playout delay and last-frame seek rounding')

# Invalid external values must never reach a shell.
previous_calls = (control / 'calls').read_text()
report = run(seconds=.2, sshHost='-oProxyCommand=touch /tmp/brisas-injected')
assert report['error'] and (control / 'calls').read_text() == previous_calls
report = run(seconds=.2, remoteDirectory='~/bad;touch /tmp/brisas-injected')
assert report['error'] and (control / 'calls').read_text() == previous_calls
print('PASS: unsafe configuration rejected')

# An encoder crash preserves earlier closed segments, never the unconfirmed last file.
# A malformed earlier MP4 must fail validation before any metadata is published.
source = max((p.parent for p in root.glob('*/session.json')),
             key=lambda p: len(list((p / 'rgb').glob('*.mp4'))))
broken_root = base / 'broken-sessions'
broken = broken_root / 'interrupted'
shutil.copytree(source, broken)
shutil.rmtree(broken / 'publication')
session = json.loads((broken / 'session.json').read_text())
session['state'] = 'error'
session['rgb_exit_code'] = -1
session['depth_exit_code'] = -1
(broken / 'session.json').write_text(json.dumps(session))
(broken / 'rgb/rgb_000.mp4').write_bytes(b'not a container')
for stream in ('rgb', 'depth'):
    with (broken / stream / 'segments.csv').open('a') as out:
        out.write(f'{stream}_999.mp4,123')  # incomplete CSV row is never committed
root = broken_root
run(seconds=12)
broken_jobs = jobs()
assert broken_jobs
assert all('005' not in p.name and '999' not in p.name for p in broken_jobs)
for path, job in broken_jobs.items():
    if path.name == 'rgb_000.mp4.job.json':
        assert job['state'] == 'failed' and not job['prepared']
        assert not (remote / 'recordings' / (job['id'] + '.json')).exists()
    else:
        assert job['state'] == 'completed'
print('PASS: encoder crash recovery, incomplete CSV, corrupt MP4 rejected, final unconfirmed segment withheld')

print(f'Transfer integration tests passed: {base}')
