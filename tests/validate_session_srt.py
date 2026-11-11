#!/usr/bin/env python3
"""Exercise the existing transmitter and FFmpeg SRT against a loopback listener."""
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time

folder = Path(tempfile.mkdtemp(prefix='brisas-srt-'))
pidfile = str(folder/'encoder.pid')
with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as reserve:
    reserve.bind(('127.0.0.1',0))
    port = reserve.getsockname()[1]
binary = sys.argv[1] if len(sys.argv)>1 else 'obj/linux64/Release/tests/srt_session'
subprocess.run([binary,str(port),'offline',pidfile],check=True,timeout=20)
with (folder/'listener.log').open('w') as log:
    listener = subprocess.Popen(['ffmpeg','-hide_banner','-loglevel','warning','-i',
        f'srt://127.0.0.1:{port}?mode=listener&latency=20000',
        '-c','copy',str(folder/'received.ts')],stdout=log,stderr=log)
    try:
        time.sleep(.3)
        assert listener.poll() is None
        subprocess.run([binary,str(port),'connected',pidfile],check=True,timeout=20)
        try:
            listener.wait(timeout=5)
        except subprocess.TimeoutExpired:
            listener.terminate()
            listener.wait(timeout=5)
    finally:
        if listener.poll() is None:
            listener.kill()
            listener.wait()
subprocess.run(['ffprobe','-v','error','-show_entries','stream=width,height',str(folder/'received.ts')],check=True)
assert (folder/'received.ts').stat().st_size > 0
print('PASS actual SRT receiver has video. Artifacts:',folder)
