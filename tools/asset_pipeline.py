"""Reproducible asset preparation. Every replacement has an immutable backup.

Run with Python 3.10+. This module never uses the forum's destructive GR2 chain.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
from collections import Counter


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def save_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=True), encoding='utf-8')
    os.replace(temporary, path)


def command(args, log, cwd=None, timeout=300):
    result = subprocess.run([str(a) for a in args], cwd=cwd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=timeout)
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}); see {log}')
    return result.stdout.decode('utf-8', errors='replace')


def unpack(args):
    pack, work = Path(args.pack).resolve(), Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    state_path = work / 'archives.json'
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    for index in sorted(pack.glob('*.eix')):
        name = index.stem
        entry = state.setdefault(name, {'files': {}, 'already_unpacked': (pack / name).is_dir()})
        for source in [index, index.with_suffix('.epk')]:
            if not source.is_file():
                raise RuntimeError(f'Missing archive partner: {source}')
            backup = work / 'original-archives' / source.name
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(source, backup)
            source_hash = digest(source)
            if digest(backup) != source_hash:
                raise RuntimeError(f'Archive changed since backup: {source}')
            entry['files'][source.name] = {'bytes': source.stat().st_size, 'sha256': source_hash}
        save_json(state_path, state)
        if not (pack / name).is_dir():
            print(f'Unpacking {name}', flush=True)
            command([args.pml, '--nolog', '-u', name], work / 'logs' / f'unpack-{name}.log', pack)
            if not (pack / name).is_dir():
                raise RuntimeError(f'PML did not create {pack / name}')
            entry['unpacked_by_pipeline'] = True
            save_json(state_path, state)
        else:
            print(f'Keeping existing unpacked folder: {name}', flush=True)
    print(f'Original archives backed up; inventory: {state_path}', flush=True)


def virtual_name(relative):
    name = relative.replace('\\', '/').lower()
    return 'd:/' + name if name.startswith('ymir work/') else name


def inspect_gr2(files, inspector, work, label):
    records = []
    for offset in range(0, len(files), 128):
        listing = work / 'inspection' / f'{label}-{offset:06}.txt'
        listing.parent.mkdir(parents=True, exist_ok=True)
        listing.write_text(''.join(str(p) + '\n' for p in files[offset:offset + 128]), encoding='utf-8')
        output = command([inspector, listing], listing.with_suffix('.jsonl'), timeout=600)
        batch = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
        if len(batch) != len(files[offset:offset + 128]):
            raise RuntimeError(f'Incomplete inspector output: {listing}')
        records.extend(batch)
        print(f'{label}: inspected {offset + len(batch)}/{len(files)} GR2 files', flush=True)
    return records


def dds_info(path):
    with path.open('rb') as stream:
        header = stream.read(148)
    if len(header) < 128 or header[:4] != b'DDS ':
        return None
    return {'width': struct.unpack_from('<I', header, 16)[0],
            'height': struct.unpack_from('<I', header, 12)[0],
            'mips': struct.unpack_from('<I', header, 28)[0] or 1,
            'fourcc': header[84:88].decode('ascii', errors='replace'),
            'rgb_bits': struct.unpack_from('<I', header, 88)[0],
            'caps2': struct.unpack_from('<I', header, 112)[0]}


def inventory(args):
    pack, work = Path(args.pack).resolve(), Path(args.work).resolve()
    archives = json.loads((work / 'archives.json').read_text())
    files, gr2, counts = [], [], Counter()
    for archive in archives:
        root = pack / archive
        for path in sorted(root.rglob('*')):
            if not path.is_file():
                continue
            ext = path.suffix.lower()
            counts[ext] += 1
            item = {'path': str(path), 'archive': archive, 'relative': path.relative_to(root).as_posix(),
                    'virtual': virtual_name(path.relative_to(root).as_posix()), 'bytes': path.stat().st_size}
            if ext in {'.gr2', '.dds', '.tga', '.png', '.jpg', '.jpeg', '.bmp'}:
                item['sha256'] = digest(path)
                info = dds_info(path)
                if info:
                    item['dds'] = info
                if ext == '.gr2':
                    gr2.append(path)
            files.append(item)
    save_json(work / 'inventory.json', {'counts': counts, 'files': files})
    print(json.dumps({'files': len(files), 'extensions': counts}, indent=2), flush=True)
    records = inspect_gr2(gr2, args.inspector, work, 'baseline')
    save_json(work / 'gr2-baseline.json', records)
    print(f'Inventory complete: {len(files)} files, {len(records)} GR2 files', flush=True)


def prepare(args):
    work = Path(args.work).resolve()
    inventory = json.loads((work / 'inventory.json').read_text())['files']
    baseline = {r['path']: r for r in json.loads((work / 'gr2-baseline.json').read_text())}
    jobs = [item for item in inventory if item['path'] in baseline and 'error' not in baseline[item['path']]]
    def convert(item):
        destination = work / 'prepared' / item['archive'] / item['relative']
        destination.parent.mkdir(parents=True, exist_ok=True)
        receipt = destination.with_name(destination.name + '.receipt.json')
        if receipt.exists():
            previous = json.loads(receipt.read_text())
            if previous.get('source_sha256') == item['sha256'] and destination.exists() and digest(destination) == previous.get('output_sha256'):
                return {'item': item, 'candidate': str(destination), 'resumed': True}
        temporary = destination.with_name(destination.name + '.platform.gr2')
        log = work / 'conversion-logs' / item['archive'] / (item['relative'] + '.log')
        try:
            if digest(Path(item['path'])) != item['sha256']:
                raise RuntimeError('Source changed after inventory')
            command([args.preprocessor, 'PlatformConvert', item['path'], '-output', temporary,
                     '-pointer', '32', '-endian', 'little'], log.with_suffix('.platform.log'), timeout=90)
            command([args.preprocessor, 'CompressBitKnit2', temporary, '-output', destination], log, timeout=90)
            if not destination.is_file() or destination.stat().st_size < 64:
                raise RuntimeError('Missing output')
            save_json(receipt, {'source_sha256': item['sha256'], 'output_sha256': digest(destination)})
            return {'item': item, 'candidate': str(destination)}
        except Exception as error:
            return {'item': item, 'error': str(error)}
        finally:
            if temporary.exists():
                temporary.unlink()
    prepared = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for result in pool.map(convert, jobs):
            prepared.append(result)
            if len(prepared) % 128 == 0:
                print(f'Prepared {len(prepared)}/{len(jobs)} GR2 files', flush=True)
    save_json(work / 'prepared-gr2.json', prepared)
    candidates = [Path(r['candidate']) for r in prepared if 'candidate' in r]
    inspected = {r['path']: r for r in inspect_gr2(candidates, args.inspector, work, 'prepared')}
    accepted, rejected = [], []
    for result in prepared:
        item = result['item']
        candidate = result.get('candidate')
        verified = inspected.get(candidate, {})
        if not candidate or verified.get('semantic_sha256') != baseline[item['path']]['semantic_sha256']:
            rejected.append({'path': item['path'], 'reason': result.get('error', verified.get('error', 'Runtime fingerprint mismatch; kept original'))})
            continue
        size = Path(candidate).stat().st_size
        if digest(Path(candidate)) == item['sha256'] or size >= item['bytes']:
            rejected.append({'path': item['path'], 'reason': 'No file size reduction; keep original'})
            continue
        accepted.append({'item': item, 'candidate': candidate, 'bytes': size,
                         'semantic_sha256': verified['semantic_sha256'], 'sha256': digest(Path(candidate))})
    save_json(work / 'accepted-gr2.json', accepted)
    save_json(work / 'rejected-gr2.json', rejected)
    print(json.dumps({'accepted': len(accepted), 'kept_original': len(rejected),
                      'saved_bytes': sum(r['item']['bytes'] - r['bytes'] for r in accepted)}), flush=True)


def dependencies(args):
    work = Path(args.work).resolve()
    files = json.loads((work / 'inventory.json').read_text())['files']
    by_path = {item['path']: item for item in files}
    names = {item['virtual'] for item in files}
    pairs, missing = set(), []
    for record in json.loads((work / 'gr2-baseline.json').read_text()):
        owner = by_path[record['path']]['virtual']
        for texture in record.get('textures', []):
            dependency = texture.replace('\\', '/').lower()
            if not dependency:
                continue
            if ':' not in dependency:
                dependency = owner.rpartition('/')[0] + '/' + dependency
            if dependency in names:
                pairs.add((owner, dependency))
            else:
                missing.append({'model': owner, 'texture': dependency})
    manifest = work / 'asset_dependencies.txt'
    manifest.write_text('ASSET_DEPS_V1\n' + ''.join(a + '\t' + b + '\n' for a, b in sorted(pairs)), encoding='utf-8', newline='\n')
    save_json(work / 'missing-texture-references.json', missing)
    print(json.dumps({'models': len({a for a, _ in pairs}), 'texture_dependencies': len(pairs),
                      'missing_references': len(missing), 'manifest_bytes': manifest.stat().st_size}), flush=True)


def prepare_dds(args):
    from PIL import Image
    import numpy as np
    work = Path(args.work).resolve()
    files = json.loads((work / 'inventory.json').read_text())['files']
    allowed = {'monster', 'monster2', 'npc', 'npc2', 'pc', 'pc2', 'pc3', 'zone', 'building', 'guild', 'terrain'}
    jobs = []
    reasons = Counter()
    for item in files:
        name = item['virtual']
        if not name.startswith('d:/ymir work/') or name.split('/')[2] not in allowed:
            continue
        if Path(item['path']).suffix.lower() not in {'.tga', '.dds', '.png', '.jpg', '.jpeg', '.bmp'}:
            continue
        if item.get('dds', {}).get('fourcc') in {'DXT1', 'DXT3', 'DXT5'}:
            reasons['preserve_existing_block_compression'] += 1
            continue
        if any(part in name for part in ('/effect/', '/ui/', '/icon/', '_mask', '_shadow', '_normal', '_splat', '_alpha', '_specular')):
            reasons['special_texture'] += 1
            continue
        jobs.append(item)
    def convert(item):
        try:
            source = Path(item['path'])
            with Image.open(source) as image:
                image = image.convert('RGBA')
                width, height = image.size
                if width < 64 or height < 64 or width > 8192 or height > 8192 or width % 4 or height % 4:
                    return {'reason': 'dimensions'}
                pixels = np.asarray(image)
                if np.any(pixels[:, :, 3] != 255) or np.any(np.all(pixels[:, :, :3] == [255, 0, 255], axis=2)):
                    return {'reason': 'preserve_alpha_or_color_key'}
                original_rgb = pixels[:, :, :3].astype(np.int16)
            if digest(source) != item['sha256']:
                raise RuntimeError('Source changed after inventory')
            output_dir = work / 'texture-candidates' / item['archive'] / item['relative']
            output_dir.mkdir(parents=True, exist_ok=True)
            log = work / 'texture-logs' / item['archive'] / (item['relative'] + '.log')
            command([args.texconv, '-f', 'BC1_UNORM', '-m', '0', '-dx9', '-nogpu', '-nologo', '-y',
                     '-o', output_dir, source], log, timeout=120)
            candidates = list(output_dir.glob('*.dds')) + list(output_dir.glob('*.DDS'))
            candidates = list(dict.fromkeys(candidates))
            if len(candidates) != 1:
                raise RuntimeError('Ambiguous texture output')
            candidate = candidates[0]
            info = dds_info(candidate)
            if not info or info['fourcc'] != 'DXT1' or (info['width'], info['height']) != (width, height):
                raise RuntimeError('Unexpected output format/dimensions')
            with Image.open(candidate) as decoded:
                rgba = np.asarray(decoded.convert('RGBA'))
            if np.any(rgba[:, :, 3] != 255):
                raise RuntimeError('Output alpha changed')
            error = np.abs(original_rgb - rgba[:, :, :3].astype(np.int16))
            rmse = float(np.sqrt(np.mean(error.astype(np.float32) ** 2)))
            severe_fraction = float(np.mean(np.max(error, axis=2) > 16))
            if rmse > 3.0 or int(error.max()) > 40 or severe_fraction > 0.001:
                return {'reason': 'preserve_image_quality', 'path': item['path'], 'rmse': rmse}
            size = candidate.stat().st_size
            if size >= item['bytes']:
                return {'reason': 'no_file_size_reduction'}
            return {'item': item, 'candidate': str(candidate), 'bytes': size, 'sha256': digest(candidate),
                    'rmse': rmse, 'large_error_pixel_fraction': severe_fraction, 'dds': info}
        except Exception as error:
            return {'reason': 'conversion_error', 'path': item['path'], 'error': str(error)}
    accepted, details = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        for index, result in enumerate(pool.map(convert, jobs), 1):
            if 'candidate' in result:
                accepted.append(result)
            else:
                reasons[result['reason']] += 1
                details.append(result)
            if index % 128 == 0:
                print(f'Texture candidates {index}/{len(jobs)}; accepted {len(accepted)}', flush=True)
    save_json(work / 'accepted-dds.json', accepted)
    save_json(work / 'texture-decisions.json', {'reasons': reasons, 'details': details})
    print(json.dumps({'accepted': len(accepted), 'reasons': reasons,
                      'saved_bytes': sum(r['item']['bytes'] - r['bytes'] for r in accepted)}), flush=True)


def promote(args):
    pack, work = Path(args.pack).resolve(), Path(args.work).resolve()
    changes = json.loads((work / 'accepted-gr2.json').read_text()) + json.loads((work / 'accepted-dds.json').read_text())
    changes += [{'item': {'path': str(pack / 'root' / 'asset_dependencies.txt'), 'archive': 'root',
                         'relative': 'asset_dependencies.txt', 'sha256': None},
                 'candidate': str(work / 'asset_dependencies.txt'), 'sha256': digest(work / 'asset_dependencies.txt')}]
    for change in changes:
        source, candidate = Path(change['item']['path']), Path(change['candidate'])
        source.relative_to(pack)
        candidate.relative_to(work)
        if digest(candidate) != change['sha256']:
            raise RuntimeError(f'Prepared output changed: {candidate}')
        current = digest(source) if source.exists() else None
        if current not in {change['item']['sha256'], change['sha256']}:
            raise RuntimeError(f'Live source changed since audit: {source}')
    save_json(work / 'promotion.json', changes)
    for change in changes:
        item = change['item']
        source = Path(item['path'])
        if source.exists() and digest(source) == change['sha256']:
            continue
        if source.exists():
            backup = work / 'original-assets' / item['archive'] / item['relative']
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                shutil.copy2(source, backup)
            if digest(backup) != item['sha256']:
                raise RuntimeError(f'Backup verification failed: {backup}')
        staged = source.with_name(source.name + '.assetv3.tmp')
        shutil.copy2(change['candidate'], staged)
        os.replace(staged, source)
    print(f'Promoted {len(changes)} prepared files with verified backups', flush=True)


def archive_index(inspector, index, log):
    output = command([inspector, '--index', index], log)
    result = [json.loads(line) for line in output.splitlines() if line.startswith('{')]
    if not result:
        raise RuntimeError(f'Empty archive index: {index}')
    if any(row['type'] not in {0, 1, 2} for row in result):
        raise RuntimeError(f'Archive uses a compression type unsupported by this client: {index}')
    return result


def repack(args):
    pack, work = Path(args.pack).resolve(), Path(args.work).resolve()
    changes = json.loads((work / 'promotion.json').read_text())
    original_index = json.loads((work / 'original-pack-index.json').read_text())
    names = sorted({change['item']['archive'] for change in changes})
    receipts_path = work / 'repacked.json'
    receipts = json.loads(receipts_path.read_text()) if receipts_path.exists() else {}
    for name in names:
        if name in receipts:
            continue
        print(f'Packing and checking {name}', flush=True)
        source = pack / name
        stage = work / 'repack' / name
        expected = {}
        for path in source.rglob('*'):
            if path.is_file():
                relative = path.relative_to(source)
                expected[virtual_name(relative.as_posix())] = {'relative': relative.as_posix(), 'sha256': digest(path)}
                destination = stage / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                if not destination.exists():
                    shutil.copy2(path, destination)
                elif digest(destination) != expected[virtual_name(relative.as_posix())]['sha256']:
                    raise RuntimeError(f'Staged snapshot differs: {destination}')
        command([args.pml, '--nolog', '-p', name], work / 'logs' / f'pack-{name}.log', stage.parent, timeout=900)
        index = archive_index(args.inspector, stage.with_suffix('.eix'), work / 'logs' / f'index-{name}.jsonl')
        packed_names = {row['name'].replace('\\', '/').lower(): row for row in index}
        old_names = {row['name'].replace('\\', '/').lower(): row for row in original_index[name]}
        if not set(old_names).issubset(packed_names):
            raise RuntimeError(f'Packing omitted original archive members: {name}')
        for key, row in old_names.items():
            if packed_names[key]['type'] != row['type']:
                raise RuntimeError(f'Packing changed original compression/encryption policy: {name}: {key}')
        verify = work / 'archive-roundtrip' / name
        verify.parent.mkdir(parents=True, exist_ok=True)
        for extension in ['.eix', '.epk']:
            shutil.copy2(stage.with_suffix(extension), verify.with_suffix(extension))
        if verify.exists():
            raise RuntimeError(f'Unexpected existing verification folder: {verify}')
        command([args.pml, '--nolog', '-u', name], work / 'logs' / f'roundtrip-{name}.log', verify.parent, timeout=900)
        for key, row in packed_names.items():
            if key not in expected:
                raise RuntimeError(f'Unexpected packed member: {key}')
            entry = expected[key]
            extracted = verify / entry['relative']
            if not extracted.is_file() or digest(extracted) != entry['sha256']:
                raise RuntimeError(f'Archive round-trip mismatch: {name}/{entry["relative"]}')
        receipts[name] = {'members': len(index), 'types': dict(Counter(row['type'] for row in index)),
                          'files': {ext: {'sha256': digest(stage.with_suffix(ext)), 'bytes': stage.with_suffix(ext).stat().st_size}
                                    for ext in ['.eix', '.epk']}}
        save_json(receipts_path, receipts)
        print(f'{name}: {len(index)} members round-trip verified', flush=True)
    print(f'All {len(names)} modified archives staged and verified', flush=True)


def deploy(args):
    pack, work = Path(args.pack).resolve(), Path(args.work).resolve()
    processes = subprocess.check_output(['tasklist', '/fi', 'IMAGENAME eq metin2client.exe', '/fo', 'csv', '/nh'])
    if b'metin2client.exe' in processes.lower():
        raise RuntimeError('Close metin2client.exe before archive deployment')
    receipts = json.loads((work / 'repacked.json').read_text())
    original = json.loads((work / 'archives.json').read_text())
    for name, receipt in receipts.items():
        for extension, metadata in receipt['files'].items():
            staged = work / 'repack' / (name + extension)
            live = pack / (name + extension)
            if digest(staged) != metadata['sha256']:
                raise RuntimeError(f'Staged archive changed: {staged}')
            previous = original[name]['files'][live.name]['sha256']
            if digest(live) not in {previous, metadata['sha256']}:
                raise RuntimeError(f'Live archive changed: {live}')
    deployed = []
    for name, receipt in receipts.items():
        for extension, metadata in receipt['files'].items():
            live = pack / (name + extension)
            temporary = live.with_name(live.name + '.assetv3.tmp')
            shutil.copy2(work / 'repack' / live.name, temporary)
            os.replace(temporary, live)
            if digest(live) != metadata['sha256']:
                raise RuntimeError(f'Deployed archive verification failed: {live}')
        deployed.append(name)
        save_json(work / 'deployed.json', deployed)
        print(f'Deployed {name}', flush=True)


def rollback(args):
    pack, work = Path(args.pack).resolve(), Path(args.work).resolve()
    processes = subprocess.check_output(['tasklist', '/fi', 'IMAGENAME eq metin2client.exe', '/fo', 'csv', '/nh'])
    if b'metin2client.exe' in processes.lower():
        raise RuntimeError('Close the game before rollback')
    changes = json.loads((work / 'promotion.json').read_text())
    receipts = json.loads((work / 'repacked.json').read_text())
    original = json.loads((work / 'archives.json').read_text())
    # Preflight the entire rollback before overwriting anything.
    for change in changes:
        path = Path(change['item']['path']).resolve()
        path.relative_to(pack)
        if path.exists() and digest(path) not in {change['sha256'], change['item']['sha256']}:
            raise RuntimeError(f'Asset edited since deployment; preserve it before rollback: {path}')
    for name, receipt in receipts.items():
        for extension, metadata in receipt['files'].items():
            path = pack / (name + extension)
            expected = original[name]['files'][path.name]['sha256']
            if digest(path) not in {expected, metadata['sha256']} or digest(work / 'original-archives' / path.name) != expected:
                raise RuntimeError(f'Archive edited or backup changed: {path}')
    for change in changes:
        item = change['item']
        path = Path(item['path']).resolve()
        if item['sha256'] is None:
            if path.exists():
                path.unlink()  # Only the exact generated manifest, checked above.
        else:
            shutil.copy2(work / 'original-assets' / item['archive'] / item['relative'], path)
    for name, receipt in receipts.items():
        for extension in receipt['files']:
            path = pack / (name + extension)
            temporary = path.with_name(path.name + '.assetv3.tmp')
            shutil.copy2(work / 'original-archives' / path.name, temporary)
            os.replace(temporary, path)
    print('Restored the original archives and changed loose assets. Source changes remain separately reviewable.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('unpack')
    p.add_argument('--pack', required=True)
    p.add_argument('--work', required=True)
    p.add_argument('--pml', required=True)
    p.set_defaults(run=unpack)
    p = sub.add_parser('inventory')
    p.add_argument('--pack', required=True)
    p.add_argument('--work', required=True)
    p.add_argument('--inspector', required=True)
    p.set_defaults(run=inventory)
    p = sub.add_parser('prepare-gr2')
    p.add_argument('--work', required=True)
    p.add_argument('--inspector', required=True)
    p.add_argument('--preprocessor', required=True)
    p.add_argument('--workers', type=int, choices=range(1, 5), default=2)
    p.set_defaults(run=prepare)
    p = sub.add_parser('dependencies')
    p.add_argument('--work', required=True)
    p.set_defaults(run=dependencies)
    p = sub.add_parser('prepare-dds')
    p.add_argument('--work', required=True)
    p.add_argument('--texconv', required=True)
    p.add_argument('--workers', type=int, choices=range(1, 5), default=2)
    p.set_defaults(run=prepare_dds)
    for action, runner in [('promote', promote), ('repack', repack), ('deploy', deploy), ('rollback', rollback)]:
        p = sub.add_parser(action)
        p.add_argument('--pack', required=True)
        p.add_argument('--work', required=True)
        if action == 'repack':
            p.add_argument('--pml', required=True)
            p.add_argument('--inspector', required=True)
        p.set_defaults(run=runner)
    args = parser.parse_args()
    args.run(args)


if __name__ == '__main__':
    main()
