"""Package the FPS script from the live root archive, then deploy explicitly.

Preparation writes only inside this source workspace. All other archive members
must survive extraction/repacking byte for byte before deployment is allowed.
"""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess

SOURCE = Path(__file__).resolve().parents[1]
GAME = SOURCE.parents[3] / 'Game Client'
PACKAGE = SOURCE / 'bin' / 'crowd_cpu_fps_package_20261021_191652'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def members(folder):
    return {p.relative_to(folder).as_posix(): digest(p)
            for p in folder.rglob('*') if p.is_file()}


def pack_command(args, directory):
    process = subprocess.run([shutil.which('PackMakerLite'), '--nolog'] + args,
                             cwd=directory, capture_output=True)
    if process.returncode:
        raise RuntimeError(process.stdout.decode(errors='replace') + process.stderr.decode(errors='replace'))


def prepare():
    PACKAGE.mkdir(parents=True, exist_ok=False)
    original = PACKAGE / 'original'
    stage = PACKAGE / 'stage'
    original.mkdir()
    stage.mkdir()
    source_hashes = {}
    for name in ('root.eix', 'root.epk'):
        source = GAME / 'pack' / name
        source_hashes[name] = digest(source)
        shutil.copy2(source, original / name)
        shutil.copy2(source, stage / name)
    loose = GAME / 'pack/root/uiminimap.py'
    shutil.copy2(loose, original / 'uiminimap.py')
    pack_command(['-u', 'root'], stage)
    before = members(stage / 'root')
    target = stage / 'root/uiminimap.py'
    shutil.copy2(target, original / 'archived_uiminimap.py')
    expected = SOURCE / 'bin/crowd_cpu_fps_backup_20261021_191652/client_pack/root/uiminimap.py'
    if digest(target) != digest(expected):
        raise RuntimeError('Live archived minimap differs from the source baseline; merge required.')
    shutil.copy2(SOURCE / 'client_pack/root/uiminimap.py', target)
    pack_command(['--parallel', '-p', 'root'], stage)
    roundtrip = PACKAGE / 'roundtrip'
    roundtrip.mkdir()
    for name in ('root.eix', 'root.epk'):
        shutil.copy2(stage / name, roundtrip / name)
    pack_command(['-u', 'root'], roundtrip)
    after = members(roundtrip / 'root')
    changed = [name for name in before if before[name] != after.get(name)]
    if set(before) != set(after) or changed != ['uiminimap.py']:
        raise RuntimeError('Unexpected archive member changes: ' + repr(changed))
    if after['uiminimap.py'] != digest(target):
        raise RuntimeError('Packed minimap differs from requested source.')
    receipt = {'original_archive_hashes': source_hashes, 'members': len(before),
               'changed_members': changed, 'script_sha256': digest(target),
               'packed_hashes': {n: digest(stage / n) for n in ('root.eix', 'root.epk')}}
    (PACKAGE / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


def deploy():
    receipt = json.loads((PACKAGE / 'receipt.json').read_text())
    output = subprocess.check_output(['tasklist', '/FI', 'IMAGENAME eq metin2client.exe', '/FO', 'CSV', '/NH'])
    rows = csv.reader(io.StringIO(output.decode(errors='replace')))
    if any(row and row[0].lower() == 'metin2client.exe' for row in rows):
        raise RuntimeError('Close the game before deploying its archive.')
    for name, expected in receipt['original_archive_hashes'].items():
        if digest(GAME / 'pack' / name) != expected:
            raise RuntimeError('Live archive changed since preparation: ' + name)
    for name, expected in receipt['packed_hashes'].items():
        if digest(PACKAGE / 'stage' / name) != expected:
            raise RuntimeError('Prepared archive changed: ' + name)
    for name in ('root.eix', 'root.epk'):
        shutil.copy2(PACKAGE / 'stage' / name, GAME / 'pack' / name)
    shutil.copy2(SOURCE / 'client_pack/root/uiminimap.py', GAME / 'pack/root/uiminimap.py')
    executable = SOURCE / 'bin/Release/metin2client.exe'
    if digest(executable) != digest(GAME / 'metin2client.exe'):
        raise RuntimeError('Client executable was not deployed by the Release build.')
    for name, expected in receipt['packed_hashes'].items():
        if digest(GAME / 'pack' / name) != expected:
            raise RuntimeError('Deployed archive differs: ' + name)
    receipt['deployed_client_sha256'] = digest(executable)
    receipt['deployed'] = True
    (PACKAGE / 'receipt.json').write_text(json.dumps(receipt, indent=2))
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--deploy', action='store_true')
    arguments = parser.parse_args()
    deploy() if arguments.deploy else prepare()
