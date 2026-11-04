#!/usr/bin/env python3
"""Validate one completed session, independently decoding every MP4 segment."""
import csv
import json
from pathlib import Path
import subprocess
import sys


def probe(path, *options):
    return json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', *options, '-of', 'json', str(path)]))


def validate(directory):
    directory = Path(directory)
    session = json.loads((directory / 'session.json').read_text())
    assert session['state'] == 'completed', session
    fps = session['fps']
    frames = session['frames_written_pairs']
    rows = list(csv.DictReader((directory / 'timestamps.csv').open()))
    assert len(rows) == frames
    for index, row in enumerate(rows):
        assert int(row['frame']) == index
        assert int(row['pts_ns']) == index * 1_000_000_000 // fps
        for stream in ('rgb', 'depth'):
            received = int(row[stream + '_received_ns'])
            assert received <= int(row['pts_ns'])
        assert int(row['segment']) == index // (session['segment_seconds'] * fps)
    if session['stop_reason'] == 'user':
        requested = session['stop_requested_elapsed_ns'] / 1e9
        assert abs(frames / fps - requested) <= 1 / fps + 0.001
    counts = {}
    for stream in ('rgb', 'depth'):
        files = sorted((directory / stream).glob('*.mp4'))
        assert files, directory
        counts[stream] = []
        for index, file in enumerate(files):
            video = probe(file, '-select_streams', 'v:0', '-count_frames', '-show_streams')['streams'][0]
            assert video['codec_name'] == 'h264'
            assert video['pix_fmt'] == 'yuv420p'
            assert (video['width'], video['height']) == (640, 480)
            assert video['r_frame_rate'] == f'{fps}/1'
            assert abs(float(video['start_time'])) < 1 / fps
            count = int(video['nb_read_frames'])
            counts[stream].append(count)
            if index < len(files) - 1:
                assert count == session['segment_seconds'] * fps, (file, count)
            packet = probe(file, '-select_streams', 'v:0', '-read_intervals', '%+#1', '-show_packets')['packets'][0]
            assert 'K' in packet['flags'], file
            subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(file), '-f', 'null', '-'], check=True)
        assert sum(counts[stream]) == frames, counts
    assert counts['rgb'] == counts['depth'], counts
    assert session['depth']['quantized_bits'] == 8
    assert session['depth']['lossless'] is False
    assert session['rgb_exit_code'] == session['depth_exit_code'] == 0
    assert sum(p.stat().st_size for p in directory.rglob('*') if p.is_file()) < session['max_bytes']
    print(f'OK: {directory}: {frames} frames, {frames/fps:.3f}s, segments={counts["rgb"]}')


if __name__ == '__main__':
    target = Path(sys.argv[1])
    if (target / 'report.json').exists():
        report = json.loads((target / 'report.json').read_text())
        for name, path in report.items():
            if name == 'killed':
                # The segment closed before a process crash must remain playable.
                for stream in ('rgb', 'depth'):
                    file = Path(path) / stream / f'{stream}_000.mp4'
                    subprocess.run(['ffmpeg', '-v', 'error', '-xerror', '-i', str(file), '-f', 'null', '-'], check=True)
                    count = probe(file, '-select_streams', 'v:0', '-count_frames', '-show_streams')['streams'][0]['nb_read_frames']
                    assert int(count) == 30
                print(f'OK: completed segments survive encoder crash: {path}')
            else:
                validate(path)
        # Check the known synthetic depth bands after lossy encode/decode.
        depth_file = Path(report['normal']) / 'depth' / 'depth_001.mp4'
        gray = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(depth_file), '-frames:v', '1',
                                        '-pix_fmt', 'gray', '-f', 'rawvideo', '-'])
        for x, expected in ((80, 0), (240, 255), (400, 128), (560, 1)):
            assert abs(gray[240 * 640 + x] - expected) <= 4, (x, gray[240 * 640 + x])
    else:
        validate(target)
