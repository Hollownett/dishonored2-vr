"""Snapshot the compiler inputs and link a successful DLL to that snapshot."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid
import zipfile


SCHEMA = 1
TOOL_EXTENSIONS = {'.c', '.cc', '.cpp', '.h', '.hh', '.hpp', '.inc', '.inl',
                   '.py', '.ps1', '.cmd', '.bat'}
SOURCE_DIRS = ('src', 'third_party', 'tests')
BUILD_SCRIPTS = ('tools/build.ps1', 'tools/build-msvc.cmd', 'tools/embed_smaa.py')


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec='milliseconds').replace('+00:00', 'Z')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def source_paths(root):
    """Return all files in compiler trees plus code/build files under tools."""
    root = root.resolve()
    selected = set()
    for directory in SOURCE_DIRS:
        base = root / directory
        if not base.is_dir():
            raise FileNotFoundError(f'Missing compiler source directory: {directory}')
        for path in base.rglob('*'):
            if '__pycache__' in path.parts or path.suffix.lower() in ('.pyc', '.pyo'):
                continue
            if path.is_symlink():
                raise ValueError(f'Symlink in compiler source tree: {path.relative_to(root)}')
            if path.is_file():
                selected.add(path.relative_to(root))
    tools = root / 'tools'
    for path in tools.rglob('*'):
        if path.is_symlink():
            raise ValueError(f'Symlink in build tools: {path.relative_to(root)}')
        if path.is_file() and path.suffix.lower() in TOOL_EXTENSIONS:
            selected.add(path.relative_to(root))
    for name in BUILD_SCRIPTS:
        path = root / name
        if not path.is_file():
            raise FileNotFoundError(f'Missing build script: {name}')
        selected.add(Path(name))
    return sorted(selected, key=lambda path: path.as_posix().casefold())


def source_records(root):
    records = []
    for relative in source_paths(root):
        path = root / relative
        data = path.read_bytes()
        records.append({'path': relative.as_posix(), 'size': len(data), 'sha256': digest(data)})
    return records


def safe_relative(root, path):
    resolved = path.resolve()
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError as error:
        raise ValueError(f'Path is outside workspace: {path}') from error


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def begin(root, dll_name, mode, script_args, compiler_args):
    root = root.resolve()
    artifacts = root / 'artifacts' / 'build-source'
    artifacts.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    archive = artifacts / f'{stamp}-{uuid.uuid4().hex[:8]}'
    archive.mkdir()
    tree = archive / 'source'
    tree.mkdir()
    records = []
    for relative in source_paths(root):
        source = root / relative
        data = source.read_bytes()
        target = tree / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        records.append({'path': relative.as_posix(), 'size': len(data), 'sha256': digest(data)})

    manifest = {
        'schema': SCHEMA,
        'created_utc': utc_now(),
        'snapshot_kind': 'pre-build working-tree source snapshot',
        'compilation_from_frozen_snapshot': False,
        'roots': list(SOURCE_DIRS),
        'tools_code_extensions': sorted(TOOL_EXTENSIONS),
        'files': records,
    }
    manifest_path = archive / 'source-manifest.json'
    write_json(manifest_path, manifest)
    zip_path = archive / 'source.zip'
    with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for record in records:
            path = tree / Path(record['path'])
            bundle.write(path, 'source/' + record['path'])

    # Detect edits while the archive itself was being assembled.
    after_records = source_records(root)
    stable_at_snapshot = records == after_records
    session = {
        'schema': SCHEMA,
        'state': 'snapshot_ready' if stable_at_snapshot else 'source_changed_during_snapshot',
        'archive_dir': safe_relative(root, archive),
        'dll_name': dll_name,
        'mode': mode,
        'script_args': script_args,
        'compiler_args': compiler_args,
        'build_started_utc': utc_now(),
        'snapshot_stable': stable_at_snapshot,
        'source_manifest_sha256': sha256_file(manifest_path),
        'source_zip_sha256': sha256_file(zip_path),
    }
    session_path = archive / 'build-session.json'
    write_json(session_path, session)
    if not stable_at_snapshot:
        raise RuntimeError(f'Source changed while snapshotting; archive retained at {archive}')
    return session_path


def finish(root, session_path, dll_path):
    root = root.resolve()
    session_path = session_path.resolve()
    session = json.loads(session_path.read_text(encoding='utf-8'))
    archive = (root / session['archive_dir']).resolve()
    try:
        archive.relative_to((root / 'artifacts' / 'build-source').resolve())
    except ValueError as error:
        raise ValueError('Build session archive path is outside artifacts/build-source') from error
    if session.get('state') != 'snapshot_ready' or not session.get('snapshot_stable'):
        raise RuntimeError('Build snapshot is not stable and ready')
    if dll_path.name != session['dll_name']:
        raise ValueError(f"DLL name differs from snapshot: expected {session['dll_name']}")
    dll_path = dll_path.resolve()
    safe_relative(root, dll_path)
    if not dll_path.is_file():
        raise FileNotFoundError(f'Built DLL is missing: {dll_path}')

    manifest_path = archive / 'source-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    expected = manifest['files']
    actual = source_records(root)
    expected_by_path = {record['path']: record for record in expected}
    actual_by_path = {record['path']: record for record in actual}
    changed = [path for path in expected_by_path.keys() & actual_by_path.keys()
               if expected_by_path[path] != actual_by_path[path]]
    added = sorted(actual_by_path.keys() - expected_by_path.keys())
    deleted = sorted(expected_by_path.keys() - actual_by_path.keys())
    unchanged = not changed and not added and not deleted
    completed = utc_now()
    details = {
        'schema': SCHEMA,
        'state': 'complete' if unchanged else 'source_changed_during_build',
        'dll_name': dll_path.name,
        'dll_sha256': sha256_file(dll_path),
        'mode': session['mode'],
        'script_args': session['script_args'],
        'compiler_args': session['compiler_args'],
        'build_started_utc': session['build_started_utc'],
        'build_finished_utc': completed,
        'source_unchanged_during_build': unchanged,
        'compiled_from_frozen_snapshot': False,
        'archive_dir': session['archive_dir'],
        'source_manifest': (Path(session['archive_dir']) / 'source-manifest.json').as_posix(),
        'source_manifest_sha256': sha256_file(manifest_path),
        'source_zip': (Path(session['archive_dir']) / 'source.zip').as_posix(),
        'source_zip_sha256': sha256_file(archive / 'source.zip'),
        'source_changes': {'changed': sorted(changed), 'added': added, 'deleted': deleted},
    }
    write_json(archive / 'build-result.json', details)
    if not unchanged:
        session['state'] = 'source_changed_during_build'
        write_json(session_path, session)
        raise RuntimeError(f'Core compiler sources changed during build; see {archive / "build-result.json"}')

    pointer = root / 'build' / (dll_path.name + '.source.json')
    pointer.parent.mkdir(parents=True, exist_ok=True)
    temporary = pointer.with_name(pointer.name + '.tmp')
    write_json(temporary, details)
    temporary.replace(pointer)
    session['state'] = 'complete'
    session['build_finished_utc'] = completed
    write_json(session_path, session)
    return pointer


def matching_provenance(root, dll_name, dll_sha256):
    """Return verified pointer/manifest/zip paths, or None for an old/mismatched DLL."""
    root = root.resolve()
    pointer_path = root / 'build' / (dll_name + '.source.json')
    if not pointer_path.is_file():
        return None
    pointer = json.loads(pointer_path.read_text(encoding='utf-8'))
    if pointer.get('dll_name') != dll_name or pointer.get('dll_sha256') != dll_sha256:
        return None
    if pointer.get('state') != 'complete' or pointer.get('source_unchanged_during_build') is not True:
        return None
    manifest_path = (root / pointer['source_manifest']).resolve()
    zip_path = (root / pointer['source_zip']).resolve()
    safe_relative(root, manifest_path)
    safe_relative(root, zip_path)
    if not manifest_path.is_file() or not zip_path.is_file():
        raise ValueError('Matching build provenance archive is incomplete')
    if sha256_file(manifest_path) != pointer.get('source_manifest_sha256'):
        raise ValueError('Matching build source manifest hash changed')
    if sha256_file(zip_path) != pointer.get('source_zip_sha256'):
        raise ValueError('Matching build source zip hash changed')
    return {
        'pointer': pointer_path,
        'manifest': manifest_path,
        'zip': zip_path,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    start = sub.add_parser('begin')
    start.add_argument('--root', type=Path, required=True)
    start.add_argument('--dll-name', required=True)
    start.add_argument('--mode', required=True)
    start.add_argument('--script-arg', action='append', default=[])
    start.add_argument('--compiler-arg', action='append', default=[])
    end = sub.add_parser('finish')
    end.add_argument('--root', type=Path, required=True)
    end.add_argument('--session', type=Path, required=True)
    end.add_argument('--dll', type=Path, required=True)
    failed = sub.add_parser('failed')
    failed.add_argument('--root', type=Path, required=True)
    failed.add_argument('--session', type=Path, required=True)
    failed.add_argument('--exit-code', type=int, required=True)
    args = parser.parse_args()
    if args.command == 'begin':
        session = begin(args.root, args.dll_name, args.mode, args.script_arg, args.compiler_arg)
        print(json.dumps({'session': str(session)}))
    elif args.command == 'finish':
        pointer = finish(args.root, args.session, args.dll)
        print(json.dumps({'provenance': str(pointer)}))
    else:
        root = args.root.resolve()
        session_path = args.session.resolve()
        session = json.loads(session_path.read_text(encoding='utf-8'))
        archive = (root / session['archive_dir']).resolve()
        try:
            archive.relative_to((root / 'artifacts' / 'build-source').resolve())
        except ValueError as error:
            raise ValueError('Build session archive path is outside artifacts/build-source') from error
        session['state'] = 'build_failed'
        session['build_finished_utc'] = utc_now()
        session['exit_code'] = args.exit_code
        write_json(archive / 'build-result.json', session)
        write_json(session_path, session)
        print(json.dumps({'state': 'build_failed', 'archive': str(archive)}))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'build source archive: {error}', file=sys.stderr)
        raise
