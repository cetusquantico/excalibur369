#!/usr/bin/env python3
"""File ownership and durability checks; only disposable files and known fixtures."""
import fcntl
import json
import os
from pathlib import Path
import resource
import signal
import stat
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
checks = 0

def run(args, valid=True, **kwargs):
    global checks
    p = subprocess.run([binary, *args], capture_output=True, text=True, timeout=30, **kwargs)
    checks += 1
    assert (p.returncode == 0) == valid, (args, p.returncode, p.stdout, p.stderr)
    return p

base = ['--range', '1000:1fff', '--hash160', '0' * 40, '--threads', '1', '--quiet']
with tempfile.TemporaryDirectory(prefix='ex369-run-files-') as temp:
    root = Path(temp)
    output = root / 'result.jsonl'
    for key in ('1', '4b5f8303e9'):
        run(['--verify', key, '--output', str(output)])
    records = [json.loads(s) for s in output.read_text().splitlines()]
    assert [r['address'] for r in records] == ['1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH', '122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8']
    assert all(r['verified'] and r['schema'] == 1 for r in records)
    assert stat.S_IMODE(output.stat().st_mode) == 0o600
    # Actual reporting path, with a one-element public known vector.
    run(['--range', '1:1', '--hash160', '751e76e8199196d454941c45d1b3a323f1433bd6',
         '--threads', '1', '--output', str(output), '--mode', 'reverse'])
    records = [json.loads(s) for s in output.read_text().splitlines()]
    assert len(records) == 3 and len(records[-1]['config_hash']) == 32
    saved = output.read_bytes()
    run(['--verify', '0', '--output', str(output)], False)
    assert output.read_bytes() == saved

    cp = root / 'state.json'
    lock = root / 'state.json.lock'
    with lock.open('w') as held:
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        busy = run(base + ['--checkpoint', str(cp)], False)
        assert 'busy' in busy.stderr and not cp.exists()
        alias = root / 'alias'
        alias.symlink_to(root, target_is_directory=True)
        run(base + ['--checkpoint', str(alias / cp.name)], False)
        # Read-only planning creates no state and does not need the writer lock.
        run(base + ['--checkpoint', str(cp), '--output', str(root/'dry.jsonl'), '--dry-run'])
        assert not (root/'dry.jsonl').exists() and not (root/'dry.jsonl.lock').exists()
    run(base + ['--checkpoint', str(cp)])
    assert json.loads(cp.read_text())['watermark'] == 1
    with (root/'result.jsonl.lock').open('w') as held:
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        run(['--verify', '1', '--output', str(output)], False)
        assert output.read_bytes() == saved

    for args in [
        ['--checkpoint', str(output), '--output', str(output)],
        ['--checkpoint', str(root/'result.jsonl.lock'), '--output', str(output)],
        ['--checkpoint', str(cp), '--output', str(lock)],
    ]:
        run(base + args, False)
    targets = root/'targets.txt'
    targets.write_text('0'*40 + '\n')
    original = targets.read_bytes()
    for flag in ('--checkpoint', '--output'):
        run(base + ['--file', str(targets), flag, str(targets)], False)
        assert targets.read_bytes() == original

    symlink = root/'linked.jsonl'
    symlink.symlink_to(output)
    run(['--verify', '1', '--output', str(symlink)], False)
    hardlink = root/'hardlinked.jsonl'
    os.link(output, hardlink)
    run(['--verify', '1', '--output', str(hardlink)], False)
    hardlink.unlink()
    run(['--verify', '1', '--output', str(root)], False)
    output.chmod(0o644)
    run(['--verify', '1', '--output', str(output)], False)
    output.chmod(0o600)
    output.write_bytes(saved + b'{"incomplete":')
    run(['--verify', '1', '--output', str(output)], False)
    assert output.read_bytes() == saved + b'{"incomplete":'
    output.write_bytes(saved)

    def no_file_growth():
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
    failed = run(['--range', '1:1', '--hash160', '751e76e8199196d454941c45d1b3a323f1433bd6',
                  '--threads', '1', '--output', str(root/'full.jsonl')], False, preexec_fn=no_file_growth)
    assert 'result output' in failed.stderr

    # A downstream process closing stdout cannot discard an already confirmed
    # journal record. Use only the one-element known public vector.
    read_end, write_end = os.pipe()
    os.close(read_end)
    try:
        pipe_result = root/'closed-pipe.jsonl'
        subprocess.run([binary, '--range', '1:1', '--hash160', '751e76e8199196d454941c45d1b3a323f1433bd6',
                        '--threads', '1', '--quiet', '--output', str(pipe_result)],
                       stdout=write_end, stderr=subprocess.PIPE, timeout=20)
        assert json.loads(pipe_result.read_text())['address'] == '1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH'
    finally:
        os.close(write_end)

    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
        state = root / f'signal-{sig}.json'
        args = ['--range', '40000:7ffff', '--hash160', '0'*40, '--threads', '1', '--batch', '1',
                '--segment-size', '64', '--mode', 'reverse', '--checkpoint', str(state),
                '--checkpoint-interval', '1', '--quiet']
        child = subprocess.Popen([binary, *args], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        interrupted = False
        try:
            deadline = time.monotonic() + 25
            # A published checkpoint is a synchronization point after handlers
            # were installed. Very fast CPUs may finish this tiny workload first.
            while not state.exists() and child.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            assert state.exists(), 'no checkpoint published in time'
            if child.poll() is None:
                child.send_signal(sig)
                interrupted = True
            _, stderr = child.communicate(timeout=10)
            allowed = (0, -signal.SIGKILL) if sig == signal.SIGKILL else (0,)
            assert child.returncode in allowed, (child.returncode, stderr)
        finally:
            if child.poll() is None:
                child.kill(); child.communicate()
        run(args + ['--resume', '--batch', '128'])
        data = json.loads(state.read_text())
        assert data['watermark'] == 4096 and data['done_above'] == []
        assert int(data['total_candidates']) >= 262144
        print(f'signal {sig}: interrupted={interrupted}, complete final watermark')

print(f'test_run_files: {checks} process checks passed')
