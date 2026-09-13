#!/usr/bin/env python3
"""Initialize pinned dependencies and preserve reviewed patches across updates.

Only Git, CMake and Python 3.10+ are needed. Run again after pulling the
superproject. --check is offline and does not write files or Git configuration.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


class SetupError(RuntimeError):
    pass


def run(args, cwd, *, check=True, capture=True):
    env = dict(os.environ, GIT_OPTIONAL_LOCKS='0')
    result = subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if check and result.returncode:
        detail = (result.stderr or result.stdout or b'').decode(errors='replace').strip()
        raise SetupError(f"Command failed in {cwd}: {' '.join(map(str, args))}\n{detail}")
    return result


def git(root, *args, **kwargs):
    return run(['git', '-c', 'core.fsmonitor=false', *args], root, **kwargs)


def digest(data):
    return hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest()


def source_path(root, relative):
    path = PurePosixPath(relative)
    if not relative or path.is_absolute() or '..' in path.parts or '\\' in relative:
        raise SetupError(f'Invalid dependency path: {relative}')
    current = root
    for part in path.parts:
        current = current / part
        if current.is_symlink():
            raise SetupError(f'Dependency patch expects a regular file: {current}')
    return current


def file_hash(path):
    if path.is_symlink() or path.is_dir():
        raise SetupError(f'Dependency patch expects a regular file: {path}')
    return digest(path.read_bytes()) if path.exists() else 'absent'


def git_path(root, name):
    value = git(root, 'rev-parse', '--git-path', name).stdout.decode().strip()
    path = Path(value)
    return path if path.is_absolute() else root / path


def initialized(path):
    return (path / '.git').exists()


def head(path):
    return git(path, 'rev-parse', 'HEAD').stdout.decode().strip()


def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise SetupError(f'Cannot read {path}: {error}') from error


def write_json(path, value):
    content = (json.dumps(value, indent=2, sort_keys=True) + '\n').encode()
    if path.exists() and path.read_bytes() == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_bytes(content)
    temporary.replace(path)


@contextmanager
def setup_lock(root):
    """Share one advisory lock with CMake and other checkout/build directories."""
    path = git_path(root, 'liberty-dependency-patches.lock')
    with path.open('a+b') as stream:
        if os.name == 'nt':
            import msvcrt
            if path.stat().st_size == 0:
                stream.write(b'\0')
                stream.flush()
        else:
            import fcntl
        deadline = time.monotonic() + 30
        while True:
            try:
                stream.seek(0)
                if os.name == 'nt':
                    msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise SetupError('Another dependency setup is running. Retry when it finishes.')
                time.sleep(0.1)
        try:
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def submodules(root):
    """Read the current gitlinks and .gitmodules, including staged pin changes."""
    links = {}
    for row in git(root, 'ls-files', '--stage', '-z').stdout.split(b'\0'):
        if not row:
            continue
        metadata, raw_path = row.split(b'\t', 1)
        mode, revision, stage = metadata.decode().split()
        if stage != '0':
            raise SetupError(f'Unresolved Git conflict: {root / os.fsdecode(raw_path)}')
        if mode == '160000':
            links[os.fsdecode(raw_path)] = revision
    config = root / '.gitmodules'
    declarations = {}
    if config.exists():
        result = git(root, 'config', '--null', '--file', config, '--get-regexp',
                     r'^submodule\..*\.(path|url)$', check=False)
        if result.returncode not in (0, 1):
            raise SetupError(f'Invalid .gitmodules in {root}')
        for row in result.stdout.split(b'\0'):
            if row:
                key, value = row.decode().split('\n', 1)
                section, field = key.rsplit('.', 1)
                declarations.setdefault(section, {})[field] = value
    urls = {}
    for values in declarations.values():
        path, url = values.get('path'), values.get('url')
        if not path or not url or path in urls:
            raise SetupError(f'Missing/duplicate submodule path or URL in {config}: {values}')
        source_path(root, path)
        urls[path] = url
    if set(links) != set(urls):
        missing = sorted(set(links) - set(urls))
        stale = sorted(set(urls) - set(links))
        raise SetupError(f'{config}: missing mappings {missing}; stale mappings {stale}')
    return [{'path': path, 'revision': revision, 'url': urls[path]}
            for path, revision in sorted(links.items())]


def portable_paths(root):
    seen = {}
    reserved = re.compile(r'^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)', re.I)
    for raw in git(root, 'ls-files', '-z').stdout.split(b'\0'):
        if not raw:
            continue
        name = os.fsdecode(raw)
        if re.search(r'[<>:"|?*\x00-\x1f]', name) or any(
                part.endswith((' ', '.')) or reserved.match(part) for part in name.split('/')):
            raise SetupError(f'Tracked path cannot be checked out on Windows: {name}')
        folded = name.casefold()
        if folded in seen and seen[folded] != name:
            raise SetupError(f'Case-insensitive path collision: {seen[folded]} and {name}')
        seen[folded] = name


def inventory(root, *, require_ready=False):
    entries = []
    for item in submodules(root):
        path = source_path(root, item['path'])
        entries.append((root, item))
        if initialized(path):
            if require_ready and head(path) != item['revision']:
                raise SetupError(f'{path} is at {head(path)}, expected {item["revision"]}. Run setup again.')
            entries.extend(inventory(path, require_ready=require_ready))
        elif require_ready:
            raise SetupError(f'Dependency is not initialized: {path}. Run tools/setup_repo.py.')
    return entries


class Patches:
    def __init__(self, root, directory=None):
        self.root = root
        self.directory = directory or root / 'cmake/dependency-patches'
        self.manifest = read_json(self.directory / 'manifest.json')
        if self.manifest.get('schema') != 1:
            raise SetupError('Unsupported dependency patch manifest schema')
        self.entries = self.manifest['dependencies']
        self.receipt_path = git_path(root, 'liberty-dependency-state.json')

    def validate(self):
        paths = set()
        for entry in self.entries:
            source = source_path(self.root, entry['path'])
            if entry['path'] in paths:
                raise SetupError(f'Duplicate patched dependency: {entry["path"]}')
            paths.add(entry['path'])
            patch = source_path(self.directory, entry['patch'])
            if hashlib.sha256(patch.read_bytes()).hexdigest() != entry['sha256']:
                raise SetupError(f'Dependency patch checksum mismatch: {patch}')
            names = set()
            for item in entry['files']:
                source_path(source, item['path'])
                if item['path'] in names:
                    raise SetupError(f'Duplicate patched file: {item["path"]}')
                names.add(item['path'])

    def remember(self, entries):
        previous = read_json(self.receipt_path) if self.receipt_path.exists() else {'dependencies': []}
        merged = {item['path']: item for item in previous['dependencies']}
        merged.update({item['path']: item for item in entries})
        write_json(self.receipt_path, {'schema': 1, 'dependencies': list(merged.values())})

    def prepare(self, only=None, *, check=False):
        self.validate()
        planned, processed = [], []
        if only and only not in {entry['name'] for entry in self.entries}:
            raise SetupError(f'Unknown dependency patch: {only}')
        for entry in self.entries:
            if only and entry['name'] != only:
                continue
            source = source_path(self.root, entry['path'])
            if not initialized(source):
                if check:
                    raise SetupError(f'Dependency not initialized: {source}')
                continue  # Standalone/platform builds may initialize only their dependencies.
            includes = []
            for item in entry['files']:
                path = source_path(source, item['path'])
                current = file_hash(path)
                if current == item['after_sha256']:
                    continue
                if current != item['before_sha256']:
                    raise SetupError(f'Local dependency changes differ from the reviewed patch: {path}\n'
                                     'No source files have been changed. Preserve and review the local edits first.')
                if check:
                    raise SetupError(f'Dependency patch is not applied: {path}. Run tools/setup_repo.py.')
                includes.append('--include=' + item['path'])
            if includes:
                command = ['apply', '--ignore-space-change', *includes, self.directory / entry['patch']]
                git(source, command[0], '--check', *command[1:])
                planned.append((source, command))
            processed.append(entry)
        if check:
            return
        for source, command in planned:
            git(source, *command)
            print(f'Applied reviewed dependency patch: {source}', flush=True)
        for entry in processed:
            for item in entry['files']:
                path = source_path(self.root / entry['path'], item['path'])
                if file_hash(path) != item['after_sha256']:
                    raise SetupError(f'Dependency post-patch checksum mismatch: {path}')
        self.remember(processed)

    def migration_plan(self, modules):
        """Preflight every affected worktree before undoing any managed changes."""
        self.validate()
        history = list(self.entries)
        for filename in self.manifest.get('legacy_manifests', []):
            history.extend(read_json(source_path(self.directory, filename))['dependencies'])
        if self.receipt_path.exists():
            history.extend(read_json(self.receipt_path)['dependencies'])
        desired = {entry['path']: entry for entry in self.entries}
        actions = []
        for parent, module in modules:
            source = source_path(parent, module['path'])
            if not initialized(source):
                if source.exists() and any(source.iterdir()):
                    raise SetupError(f'Unversioned dependency directory is not empty: {source}\n'
                                     'Keep it as a backup outside this path, then rerun setup.')
                continue
            relative = source.relative_to(self.root).as_posix()
            revision = head(source)
            target = desired.get(relative)
            candidates = [entry for entry in reversed(history)
                          if entry['path'] == relative and entry['base_revision'] == revision]
            recognized = None
            for entry in candidates:
                if all(file_hash(source_path(source, item['path'])) in
                       (item['before_sha256'], item['after_sha256']) for item in entry['files']):
                    recognized = entry
                    break
            # A pinned dependency may also need a patch-only upgrade/removal.
            patch_changed = recognized and (not target or recognized['sha256'] != target['sha256'])
            if revision == module['revision'] and not patch_changed:
                continue
            if git(source, 'diff', '--cached', '--quiet', check=False).returncode:
                raise SetupError(f'Staged dependency edits must be preserved before updating: {source}')
            owned = {item['path']: item for item in recognized['files']} if recognized else {}
            changed = git(source, 'diff', '--name-only', '-z').stdout.split(b'\0')
            for raw in changed:
                if raw and os.fsdecode(raw) not in owned:
                    raise SetupError(f'Unrecognized local edit blocks dependency update: {source / os.fsdecode(raw)}')
            restore = []
            for name, item in owned.items():
                path = source_path(source, name)
                current = file_hash(path)
                if current == item['before_sha256']:
                    continue
                if current != item['after_sha256']:
                    raise SetupError(f'Unrecognized local edit blocks dependency update: {path}')
                original = git(source, 'show', revision + ':' + name, check=False)
                data = original.stdout if original.returncode == 0 else None
                if (digest(data) if data is not None else 'absent') != item['before_sha256']:
                    raise SetupError(f'Previous dependency baseline does not match its receipt: {path}')
                restore.append((path, data))
            actions.extend(restore)
        # Verify new patch files even when no pin changes. This catches conflicts
        # before changing another dependency, including newly owned untracked files.
        reverting = {path for path, _ in actions}
        for entry in self.entries:
            source = source_path(self.root, entry['path'])
            if not initialized(source):
                continue
            for item in entry['files']:
                path = source_path(source, item['path'])
                if path in reverting:
                    continue
                current = file_hash(path)
                if current in (item['before_sha256'], item['after_sha256']):
                    continue
                # A clean legacy committed file can differ from the new public
                # baseline. Git will replace it when the recorded pin changes.
                original = git(source, 'show', 'HEAD:' + item['path'], check=False)
                if original.returncode == 0 and current == digest(original.stdout):
                    continue
                raise SetupError(f'Unrecognized local edit blocks dependency update: {path}')
        return actions


def check_tools():
    for name in ('git', 'cmake'):
        if not shutil.which(name):
            raise SetupError(f'{name} is required. See docs/BUILDING.md for prerequisites.')
    version = run(['cmake', '--version'], ROOT).stdout.decode()
    match = re.search(r'cmake version (\d+)\.(\d+)', version)
    if not match or tuple(map(int, match.groups())) < (3, 29):
        raise SetupError('CMake 3.29 or newer is required. CMake 4 is supported.')


def bootstrap_vcpkg(root):
    source = root / 'thirdparty/vcpkg'
    if not initialized(source):
        return
    executable = source / ('vcpkg.exe' if os.name == 'nt' else 'vcpkg')
    stamp = git_path(root, 'liberty-vcpkg-revision')
    revision = head(source)
    if executable.is_file() and stamp.exists() and stamp.read_text().strip() == revision:
        return
    script = source / ('bootstrap-vcpkg.bat' if os.name == 'nt' else 'bootstrap-vcpkg.sh')
    command = ['cmd', '/d', '/c', script, '-disableMetrics'] if os.name == 'nt' else ['sh', script, '-disableMetrics']
    run(command, source, capture=False)
    stamp.write_text(revision + '\n')


def setup(root, patches, jobs=4, *, bootstrap=True):
    portable_paths(root)
    modules = inventory(root)
    actions = patches.migration_plan(modules)
    # Retain the old receipt until the replacement has been fully verified. A
    # network interruption leaves either recognized base or recognized patched
    # bytes, so another run can resume without a reset/clean/force operation.
    for path, data in actions:
        if data is None:
            path.unlink()
        else:
            path.write_bytes(data)
    git(root, 'submodule', 'sync', '--recursive', capture=False)
    git(root, '-c', 'submodule.recurse=false', 'submodule', 'update', '--init',
        '--recursive', '--checkout', '--depth', '1', '--jobs', str(jobs), capture=False)
    inventory(root, require_ready=True)
    patches.prepare()
    if bootstrap:
        bootstrap_vcpkg(root)
    print('All pinned dependencies are initialized and reviewed patches are ready.', flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true', help='Validate all dependencies offline, without changes')
    mode.add_argument('--check-metadata', action='store_true', help='Validate tracked paths, mappings and patch metadata offline')
    mode.add_argument('--prepare-only', action='store_true', help='Apply patches to initialized dependencies without downloading')
    parser.add_argument('--jobs', type=int, default=4, help='Parallel submodule downloads (default: 4)')
    parser.add_argument('--root', type=Path, default=ROOT, help=argparse.SUPPRESS)
    parser.add_argument('--patch-directory', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--only', help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        if sys.version_info < (3, 10):
            raise SetupError('Python 3.10 or newer is required.')
        if args.jobs < 1:
            raise SetupError('--jobs must be positive')
        root = args.root.resolve()
        patches = Patches(root, args.patch_directory.resolve() if args.patch_directory else None)
        if args.check or args.check_metadata:
            portable_paths(root)
            modules = inventory(root, require_ready=args.check)
            patches.validate()
            direct = {item['path']: item['revision'] for parent, item in modules if parent == root}
            for entry in patches.entries:
                if direct.get(entry['path']) != entry['base_revision']:
                    raise SetupError(f'Patch base does not match gitlink: {entry["path"]}')
            if args.check:
                patches.prepare(check=True)
            print('Repository checks passed.' if args.check_metadata else 'All dependencies and patches are ready.')
        else:
            check_tools()
            with setup_lock(root):
                if args.prepare_only:
                    patches.prepare(args.only)
                else:
                    setup(root, patches, args.jobs)
        return 0
    except (SetupError, OSError, ValueError, KeyError) as error:
        print(f'LibertyRecomp setup: {error}', file=sys.stderr)
        print('See docs/BUILDING.md. Fix the reported issue and rerun the same command.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
