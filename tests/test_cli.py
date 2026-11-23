#!/usr/bin/env python3
"""CLI and persistence regression tests using disposable, tiny dummy ranges."""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
checks = 0

def run(args, valid=True):
    global checks
    p = subprocess.run([binary, *args], capture_output=True, text=True, timeout=20)
    checks += 1
    assert (p.returncode == 0) == valid, (args, p.returncode, p.stdout, p.stderr)
    return p

base = ['--range','1000:1fff','--hash160','0'*40,'--threads','1','--quiet']
with tempfile.TemporaryDirectory(prefix='ex369-cli-') as temp:
    root = Path(temp)
    for flag, value in [('--threads','2junk'),('--threads','-1'),('--threads','0'),('--batch','-1'),
                        ('--seed','oops'),('--seed','18446744073709551616'),('--status','oops'),
                        ('--status','2147483647'),('--simd','unknown'),('--simd','avx512'),
                        ('--puzzle','-1'),('--segment-size',str(2**128)),('--segment-size','0'),
                        ('--fixed-values',''),('--fixed-positions','1,')]:
        run(base + [flag,value,'--dry-run'],False)
    for key in ('0','fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141'):
        run(['--verify',key],False)
    run(base+['--resume','--dry-run'],False)
    cp = root/'checkpoint.json'
    run(base+['--resume','--checkpoint',str(cp),'--dry-run'],False)
    cp.write_text('incomplete')
    run(base+['--resume','--checkpoint',str(cp),'--dry-run'],False)

    args = base+['--segment-size','64','--limit','64','--checkpoint',str(cp)]
    run(args)
    before=json.loads(cp.read_text())
    run(args+['--resume'])
    after=json.loads(cp.read_text())
    assert before['watermark']==1 and after['watermark']==2
    assert before['total_candidates']=='64' and after['total_candidates']=='128'
    assert after['elapsed_seconds'] >= before['elapsed_seconds']
    assert after['version']==2 and len(after['integrity_hash'])==64
    for update in ({'version':999},{'watermark':65},{'watermark':3},{'done_above':[4,4]},
                   {'done_above':[-1]},{'elapsed_seconds':float('nan')},{'total_candidates':'oops'}):
        cp.write_text(json.dumps(dict(after,**update)))
        run(args+['--resume','--dry-run'],False)
    raw=json.dumps(after)
    for text in (raw[:-1],raw+'trailing',raw.replace('"version": 2','"version": 2, "version": 2')):
        cp.write_text(text)
        run(args+['--resume','--dry-run'],False)
    # Legacy aligned, unmasked checkpoints can be resumed without a migration write.
    legacy=dict(after,version=1); legacy.pop('integrity_hash')
    cp.write_text(json.dumps(legacy))
    run(args+['--resume','--dry-run'])
    assert json.loads(cp.read_text())==legacy
    # Errors while writing must be visible to automation.
    run(base+['--limit','64','--segment-size','64','--checkpoint',str(root/'missing'/'cp.json')],False)
    run(['--range','5:a','--hash160','0'*40,'--threads','1'])
    plan = run(base + ['--mode','reverse','--dry-run','--quiet'])
    # Nonquiet plan explains ordering when batching is active.
    plan = run(['--range','1000:1fff','--hash160','0'*40,'--threads','1','--mode','reverse','--dry-run'])
    assert 'reverse lane walks; no single global descending sequence' in plan.stdout
    assert 'local direction : reverse(P-=G)' in plan.stdout

    targets=root/'targets.txt'
    targets.write_text('# known public test vector\n751e76e8199196d454941c45d1b3a323f1433bd6\n')
    hit=run(['--range','1:1','--file',str(targets),'--threads','1'])
    assert '*** FOUND' in hit.stdout and '1BgGZ9tcN4rm9KBzDn7KprQz87SZ26SAMH' in hit.stdout
    targets.write_text('invalid target\n')
    run(['--range','1:2','--file',str(targets),'--dry-run'],False)
print(f'test_cli: {checks} process checks passed')
