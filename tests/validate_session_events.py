#!/usr/bin/env python3
"""Real POCO client against the current Mac contract, including registration/auth."""
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time

binary = sys.argv[1]
root = Path(tempfile.mkdtemp(prefix='brisas-events-'))
token = 'test-only-secret'
env = dict(os.environ, CORPO_EXPANDIDO_TOKEN=token)
env.pop('BRISAS_TEST_TOKEN_FILE',None)
session = {'id': 'session-from-frontend', 'iniciadaEm': '2026-11-11T18:00:00.123Z',
           'corpoPresente': {'participanteId': 'present-123', 'elemento': 'ar',
                             'corPrincipal': '#aabbcc', 'paleta': ['#aabbcc']}}

def frame(sock, value, op=1, final=True):
    data = value.encode() if isinstance(value, str) else value
    n = len(data)
    sock.sendall(bytes([op | (128 if final else 0), n if n < 126 else 126]) +
                 (struct.pack('!H', n) if n >= 126 else b'') + data)

def receive(sock):
    def exact(n):
        data = b''
        while len(data) < n:
            chunk = sock.recv(n-len(data))
            if not chunk:
                raise EOFError()
            data += chunk
        return data
    first, second = exact(2)
    n = second & 127
    if n == 126:
        n = struct.unpack('!H', exact(2))[0]
    if n == 127:
        n = struct.unpack('!Q', exact(8))[0]
    assert second & 128
    mask = exact(4)
    return first & 15, bytes(c ^ mask[i % 4] for i, c in enumerate(exact(n)))

def event(kind, value=session):
    return json.dumps({'tipo': kind, 'versao': 1, 'sessao': value})

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

class Handler(socketserver.BaseRequestHandler):
    def wait_state(self, predicate):
        while True:
            op, data = receive(self.request)
            if op == 8:
                frame(self.request,data,8)
                raise EOFError()
            if op == 9:
                self.server.pings += 1
                frame(self.request, data, 10)
                continue
            if op == 10:
                self.server.pong = data == b'probe'
                continue
            state = json.loads(data)
            assert state['tipo'] == 'dispositivo.estado' and state['versao'] == 1, state
            assert state['sessaoId'] == session['id'], 'null/foreign session status is not accepted by the backend'
            assert isinstance(state['gravando'],bool) and isinstance(state['transmitindo'],bool)
            self.server.states.append(state)
            # Like the actual backend, echo telemetry on the shared bus.
            frame(self.request,json.dumps(state))
            if predicate(state):
                return state

    def handle(self):
        try:
            self.request.settimeout(40)
            request = b''
            while b'\r\n\r\n' not in request:
                part = self.request.recv(1)
                if not part: return
                request += part
            lines = request.decode().split('\r\n')
            headers = dict(line.split(': ', 1) for line in lines if ': ' in line)
            assert lines[0] == 'GET /api/v1/eventos HTTP/1.1'
            assert headers.get('Authorization') == 'Bearer '+token
            if self.server.mode in ('401','403'):
                self.request.sendall(f'HTTP/1.1 {self.server.mode} Refused\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'.encode())
                return
            key = base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key'] +
                '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
            self.request.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + key + b'\r\n\r\n')
            active = self.server.mode in ('active','1008') or (self.server.mode == 'reconnect' and self.server.registrations > 0)
            initial = event('sessao.estado', session if active else None)
            frame(self.request,initial)  # public snapshot is not authorization
            op, data = receive(self.request)
            assert op == 1 and json.loads(data) == {'tipo':'cliente.registrar','cliente':'openframeworks','versao':1}
            self.server.registrations += 1
            self.server.connectionTimes.append(time.monotonic())
            if self.server.mode == '1008':
                frame(self.request,struct.pack('!H',1008)+b'Credencial de dispositivo necessaria',8)
                op,_ = receive(self.request)
                assert op == 8
                return
            frame(self.request,initial)  # registration accepted, private snapshot
            if self.server.mode == 'flapping':
                frame(self.request,struct.pack('!H',1013)+b'Verifique as sessoes ativas',8)
                op,_ = receive(self.request)
                assert op == 8
                return
            if self.server.mode == 'reconnect':
                if self.server.registrations == 1:
                    start = event('sessao.iniciada')
                    frame(self.request,start[:40],final=False)
                    frame(self.request,b'probe',9)
                    frame(self.request,start[40:],0)
                    self.wait_state(lambda s:s['gravando'])
                    frame(self.request,start)
                    frame(self.request,'{invalid')
                    time.sleep(.3)
                    return
                self.wait_state(lambda s:s['gravando'])
                frame(self.request,json.dumps({'tipo':'sessao.encerrada','sessaoId':'unrelated'}))
                time.sleep(.3)
                frame(self.request,json.dumps({'tipo':'sessao.encerrada','sessaoId':session['id']}))
                self.wait_state(lambda s:not s['gravando'] and not s['finalizando'])
            elif self.server.mode == 'active':
                self.wait_state(lambda s:s['gravando'])
            elif self.server.mode == 'null':
                # Known broadcasts do not trigger invalid-event logs or any reply.
                for kind in ('pessoa.criada','gravacao.atualizada','dispositivo.estado','dispositivo.desconectado'):
                    frame(self.request,json.dumps({'tipo':kind,'versao':1}))
            self.wait_state(lambda _:False)
        except (EOFError,BrokenPipeError,ConnectionResetError):
            pass
        except Exception as exc:
            self.server.errors.append(repr(exc))

def serve(mode,port=0):
    server=Server(('127.0.0.1',port),Handler)
    server.mode=mode; server.registrations=0; server.states=[]; server.errors=[]
    server.pong=False; server.pings=0; server.connectionTimes=[]
    threading.Thread(target=server.serve_forever,daemon=True).start()
    return server

def command(folder,port,seconds=5):
    return [binary,'ws',str(folder),f'ws://127.0.0.1:{port}/api/v1/eventos',str(seconds)]

def run(folder,server,seconds=5,environment=env):
    result=subprocess.run(command(folder,server.server_address[1],seconds),env=environment,
                          capture_output=True,text=True,timeout=seconds+15)
    assert result.returncode==0,result.stderr
    assert not server.errors,server.errors
    assert token not in result.stdout+result.stderr
    return json.loads(result.stdout.splitlines()[-1]),result.stderr

with serve('reconnect') as server:
    result,logs=run(root/'reconnect',server)
    assert server.registrations==2 and server.pong
    assert not result['prematurelyStopped'],result
    captures=list((root/'reconnect').glob('*/session.json'))
    assert len(captures)==1,captures
    assert all(e['recording']==result['events'][1]['recording'] for e in result['events'] if e['recording'])
    assert server.states[-1]['sessaoId']==session['id'] and not server.states[-1]['gravando']
    assert 'EOF' in logs
    server.shutdown()
print('PASS authenticated registration, fragmentation, ping/pong, duplicate starts, EOF recovery, session end, state v1')

with serve('active') as server:
    for _ in range(2): run(root/'restart',server,2)
    captures=list((root/'restart').glob('*/session.json'))
    assert len(captures)==2
    for path in captures:
        data=json.loads(path.read_text())
        assert data['sessaoId']==session['id'] and data['participanteId']=='present-123'
        assert data['state']=='completed' and data['stop_reason']=='application_exit'
    server.shutdown()
print('PASS real process restart: two preserved captures for the same session')

for mode in ('1008','401','403'):
    with serve(mode) as server:
        data,logs=run(root/mode,server,3)
        assert not data['events'] and not list((root/mode).glob('*/session.json'))
        assert 'reconexão em 30s' in logs and mode in logs
        assert server.registrations==(1 if mode=='1008' else 0)
        server.shutdown()
print('PASS denied credentials: no capture from public snapshot, close reason, 30s backoff')

with serve('flapping') as server:
    data,logs=run(root/'flapping',server,7)
    assert server.registrations==3,server.connectionTimes
    for delay in (1,2,5): assert f'reconexão em {delay}s' in logs
    assert 'código=1013 motivo=Verifique as sessoes ativas' in logs
    server.shutdown()
print('PASS valid snapshots followed by closure do not reset exponential backoff')

with socket.socket() as reserve:
    reserve.bind(('127.0.0.1',0)); port=reserve.getsockname()[1]
process=subprocess.Popen(command(root/'offline',port,6),env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
time.sleep(2)
with serve('null',port) as server:
    stdout,stderr=process.communicate(timeout=15)
    assert process.returncode==0,stderr
    assert 'reconexão em 1s' in stderr and 'reconexão em 2s' in stderr
    assert server.registrations==1 and not server.errors
    assert not server.states and not list((root/'offline').glob('*/session.json'))
    server.shutdown()
print('PASS unavailable backend: later recovery; null snapshot produces no invalid telemetry')

credential=root/'device.token'; credential.write_text(token+'\n'); credential.chmod(0o600)
with serve('null') as server:
    data,logs=run(root/'idle',server,22,dict(env,BRISAS_TEST_TOKEN_FILE=str(credential)))
    assert server.registrations==1 and server.pings>=1
    assert not server.states and len(data['events'])==1
    assert 'inválida' not in logs and 'reconexão' not in logs
    server.shutdown()
print('PASS private token file, idle connection/heartbeat, no idle telemetry or echoed-event processing')
print('Artifacts:',root)
