#!/usr/bin/env python3
"""Exercise real clone/update/patch transitions using disposable local remotes."""
import contextlib
import difflib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('setup_repo', Path(__file__).with_name('setup_repo.py'))
setup_repo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup_repo)


def command(root, *args):
    return setup_repo.git(root, *args).stdout.decode().strip()


def commit(root, message):
    command(root, 'add', '--all')
    command(root, '-c', 'user.name=Setup Tests', '-c', 'user.email=setup@example.invalid',
            '-c', 'commit.gpgsign=false', 'commit', '-qm', message)
    return command(root, 'rev-parse', 'HEAD')


def manifest(root, base, after, *, legacy=False):
    directory = root / 'cmake/dependency-patches'
    directory.mkdir(parents=True, exist_ok=True)
    before = command(root / 'dep', 'show', base + ':source.txt') + '\n'
    text = 'diff --git a/source.txt b/source.txt\n' + ''.join(difflib.unified_diff(
        before.splitlines(True), after.splitlines(True), fromfile='a/source.txt', tofile='b/source.txt'))
    text += 'diff --git a/added.txt b/added.txt\nnew file mode 100644\n' + ''.join(difflib.unified_diff(
        [], ['managed addition\n'], fromfile='/dev/null', tofile='b/added.txt'))
    (directory / 'sample.patch').write_bytes(text.encode())
    entry = {'name': 'sample', 'path': 'dep', 'base_revision': base, 'patch': 'sample.patch',
             'sha256': setup_repo.hashlib.sha256(text.encode()).hexdigest(), 'files': [
                 {'path': 'source.txt', 'before_sha256': setup_repo.digest(before.encode()),
                  'after_sha256': setup_repo.digest(after.encode())},
                 {'path': 'added.txt', 'before_sha256': 'absent',
                  'after_sha256': setup_repo.digest(b'managed addition\n')}]}
    data = {'schema': 1, 'dependencies': [entry]}
    if legacy:
        data['legacy_manifests'] = ['legacy-manifest.json']
    (directory / 'manifest.json').write_text(json.dumps(data))
    return data


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='liberty setup test ')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.environment = patch.dict(os.environ, {
            'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'protocol.file.allow',
            'GIT_CONFIG_VALUE_0': 'always'})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        remote = self.base / 'public dependency'
        remote.mkdir()
        command(remote, 'init', '-q')
        (remote / 'source.txt').write_text('upstream one\n')
        self.old_revision = commit(remote, 'first upstream version')
        (remote / 'source.txt').write_text('upstream two\n')
        self.new_revision = commit(remote, 'second upstream version')
        self.root = self.base / 'maintainer'
        self.root.mkdir()
        command(self.root, 'init', '-q')
        command(self.root, 'submodule', 'add', str(remote), 'dep')
        command(self.root / 'dep', 'checkout', '--detach', self.old_revision)
        self.old_manifest = manifest(self.root, self.old_revision, 'reviewed one\n')
        self.first_commit = commit(self.root, 'first release')

    def clone(self, name, recursive=False):
        target = self.base / name
        args = ['clone', '--quiet']
        if recursive:
            args.append('--recurse-submodules')
        command(self.base, *args, str(self.root), str(target))
        return target

    def prepare(self, root):
        with setup_repo.setup_lock(root):
            setup_repo.setup(root, setup_repo.Patches(root), bootstrap=False)

    def update_release(self, revision=None, after='reviewed two\n'):
        revision = revision or self.new_revision
        directory = self.root / 'cmake/dependency-patches'
        (directory / 'legacy-manifest.json').write_text(json.dumps(self.old_manifest))
        manifest(self.root, revision, after, legacy=True)
        # Commit the new gitlink without disturbing the old patched worktree.
        command(self.root, 'add', 'cmake')
        command(self.root, 'update-index', '--cacheinfo', f'160000,{revision},dep')
        command(self.root, '-c', 'user.name=Setup Tests', '-c', 'user.email=setup@example.invalid',
                '-c', 'commit.gpgsign=false', 'commit', '-qm', 'updated release')

    def assert_ready(self, root, text='reviewed one\n'):
        setup_repo.inventory(root, require_ready=True)
        setup_repo.Patches(root).prepare(check=True)
        self.assertEqual((root / 'dep/source.txt').read_text(), text)
        self.assertEqual((root / 'dep/added.txt').read_text(), 'managed addition\n')

    def test_plain_and_recursive_clone_prepare_identically(self):
        for recursive in (False, True):
            with self.subTest(recursive=recursive):
                root = self.clone('clone ' + str(recursive), recursive)
                self.prepare(root)
                self.assert_ready(root)
                paths = [root / 'dep/source.txt', root / 'dep/added.txt',
                         setup_repo.Patches(root).receipt_path]
                before = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in paths}
                self.prepare(root)
                self.assertEqual(before, {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in paths})

    def test_pull_migrates_managed_patches_and_keeps_local_files(self):
        root = self.clone('contributor')
        self.prepare(root)
        (root / 'dep/personal.txt').write_text('keep me\n')
        self.update_release()
        command(root, '-c', 'submodule.recurse=false', 'pull', '--ff-only')
        self.prepare(root)
        self.assert_ready(root, 'reviewed two\n')
        self.assertEqual((root / 'dep/personal.txt').read_text(), 'keep me\n')

    def test_legacy_prepared_checkout_without_receipt_migrates(self):
        setup_repo.Patches(self.root).prepare()
        setup_repo.Patches(self.root).receipt_path.unlink()
        self.update_release()
        self.prepare(self.root)
        self.assert_ready(self.root, 'reviewed two\n')

    def test_patch_only_upgrade_on_same_gitlink(self):
        setup_repo.Patches(self.root).prepare()
        self.update_release(self.old_revision)
        self.prepare(self.root)
        self.assert_ready(self.root, 'reviewed two\n')

    def test_conflicting_edit_is_preserved_before_any_source_mutation(self):
        setup_repo.Patches(self.root).prepare()
        self.update_release()
        path = self.root / 'dep/source.txt'
        path.write_text('user modification\n')
        added = self.root / 'dep/added.txt'
        original = added.read_bytes()
        with self.assertRaisesRegex(setup_repo.SetupError, 'Unrecognized local edit'):
            self.prepare(self.root)
        self.assertEqual(path.read_text(), 'user modification\n')
        self.assertEqual(added.read_bytes(), original)
        self.assertEqual(setup_repo.head(self.root / 'dep'), self.old_revision)

    def test_staged_dependency_edit_is_preserved(self):
        setup_repo.Patches(self.root).prepare()
        self.update_release()
        command(self.root / 'dep', 'add', 'source.txt')
        before = command(self.root / 'dep', 'diff', '--cached')
        with self.assertRaisesRegex(setup_repo.SetupError, 'Staged dependency edits'):
            self.prepare(self.root)
        self.assertEqual(before, command(self.root / 'dep', 'diff', '--cached'))

    def test_interrupted_update_can_resume(self):
        setup_repo.Patches(self.root).prepare()
        self.update_release()
        real_git = setup_repo.git

        def offline(root, *args, **kwargs):
            if 'submodule' in args and 'update' in args:
                raise setup_repo.SetupError('simulated network interruption')
            return real_git(root, *args, **kwargs)

        with patch.object(setup_repo, 'git', side_effect=offline):
            with self.assertRaisesRegex(setup_repo.SetupError, 'network interruption'):
                self.prepare(self.root)
        self.prepare(self.root)
        self.assert_ready(self.root, 'reviewed two\n')

    def test_nonempty_unversioned_directory_is_not_overwritten(self):
        root = self.clone('zip dependency')
        directory = root / 'dep'
        directory.mkdir(exist_ok=True)
        (directory / 'personal.txt').write_text('manual download')
        with self.assertRaisesRegex(setup_repo.SetupError, 'not empty'):
            self.prepare(root)
        self.assertEqual((directory / 'personal.txt').read_text(), 'manual download')

    def test_missing_mapping_fails_before_preparation(self):
        (self.root / '.gitmodules').write_text('')
        with self.assertRaisesRegex(setup_repo.SetupError, 'missing mappings'):
            self.prepare(self.root)
        self.assertEqual((self.root / 'dep/source.txt').read_text(), 'upstream one\n')

    def test_offline_check_does_not_create_state_or_lock(self):
        setup_repo.Patches(self.root).prepare()
        receipt = setup_repo.Patches(self.root).receipt_path
        receipt.unlink()
        lock = setup_repo.git_path(self.root, 'liberty-dependency-patches.lock')
        self.assertFalse(lock.exists())
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(setup_repo.main(['--root', str(self.root), '--check']), 0)
        self.assertFalse(lock.exists())
        self.assertFalse(receipt.exists())

    def test_windows_invalid_tracked_path_is_rejected(self):
        result = subprocess.CompletedProcess([], 0, stdout=b':-\0', stderr=b'')
        with patch.object(setup_repo, 'git', return_value=result):
            with self.assertRaisesRegex(setup_repo.SetupError, 'Windows'):
                setup_repo.portable_paths(self.root)

    def test_export_check_needs_no_git_and_writes_nothing(self):
        setup_repo.Patches(self.root).prepare()
        exported = self.base / 'private export'
        shutil.copytree(self.root / 'cmake', exported / 'cmake')
        shutil.copytree(self.root / 'dep', exported / 'dep', ignore=shutil.ignore_patterns('.git'))
        before = {str(p.relative_to(exported)): (p.read_bytes(), p.stat().st_mtime_ns)
                  for p in exported.rglob('*') if p.is_file()}
        with patch.object(setup_repo, 'git', side_effect=AssertionError('Git must not be invoked')):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(setup_repo.main(['--root', str(exported), '--check-export']), 0)
        after = {str(p.relative_to(exported)): (p.read_bytes(), p.stat().st_mtime_ns)
                 for p in exported.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_export_check_rejects_unpatched_or_modified_files_without_repair(self):
        for content in ('upstream one\n', 'unreviewed change\n'):
            with self.subTest(content=content):
                (self.root / 'dep/source.txt').write_text(content)
                checker = setup_repo.Patches(self.root, source_export=True)
                with self.assertRaisesRegex(setup_repo.SetupError, 'does not match reviewed patch'):
                    checker.verify_export()
                self.assertEqual((self.root / 'dep/source.txt').read_text(), content)


if __name__ == '__main__':
    unittest.main()
