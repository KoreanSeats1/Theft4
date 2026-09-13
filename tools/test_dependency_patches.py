#!/usr/bin/env python3
"""Verify dependency preparation using disposable source trees, never the working submodules."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / 'cmake/dependency-patches/manifest.json').read_text())


def canonical_hash(path: Path) -> str:
    if not path.exists():
        return 'absent'
    return hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest()


class DependencyPatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.originals: dict[tuple[str, str], bytes] = {}
        for dependency in MANIFEST['dependencies']:
            for item in dependency['files']:
                if item['before_sha256'] == 'absent':
                    continue
                result = subprocess.run(
                    ['git', 'show', dependency['base_revision'] + ':' + item['path']],
                    cwd=ROOT / dependency['path'], capture_output=True, check=True)
                data = result.stdout
                if hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest() != item['before_sha256']:
                    raise AssertionError('Manifest does not match the pinned upstream source')
                cls.originals[(dependency['name'], item['path'])] = data

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix='liberty-patch-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'checkout'
        self.root.mkdir()
        subprocess.run(['git', 'init', '-q'], cwd=self.root, check=True, capture_output=True)
        self.cmake = Path(self.temp.name) / 'distribution/cmake'
        self.cmake.mkdir(parents=True)
        shutil.copy2(ROOT / 'cmake/DependencyPatches.cmake', self.cmake)
        helper = self.cmake.parent / 'tools'
        helper.mkdir()
        shutil.copy2(ROOT / 'tools/setup_repo.py', helper)
        shutil.copytree(ROOT / 'cmake/dependency-patches', self.cmake / 'dependency-patches')
        for dependency in MANIFEST['dependencies']:
            source = self.root / dependency['path']
            source.mkdir(parents=True)
            subprocess.run(['git', 'init', '-q'], cwd=source, check=True, capture_output=True)
            for item in dependency['files']:
                key = (dependency['name'], item['path'])
                if key not in self.originals:
                    continue
                destination = source / item['path']
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(self.originals[key])

    def prepare(self, only: str | None = None) -> subprocess.CompletedProcess[str]:
        command = ['cmake', '-DLIBERTY_DEPENDENCY_ROOT=' + str(self.root)]
        if only:
            command.append('-DLIBERTY_DEPENDENCY_ONLY=' + only)
        command += ['-P', str(self.cmake / 'DependencyPatches.cmake')]
        return subprocess.run(command, cwd=self.root, capture_output=True, text=True, timeout=30)

    def paths(self):
        for dependency in MANIFEST['dependencies']:
            for item in dependency['files']:
                yield self.root / dependency['path'] / item['path'], item

    def snapshot(self):
        return {str(path.relative_to(self.root)): path.read_bytes() if path.exists() else None
                for path, _ in self.paths()}

    def assert_prepared(self) -> None:
        for path, item in self.paths():
            self.assertEqual(canonical_hash(path), item['after_sha256'], str(path))

    def test_clean_sources_apply_and_second_run_is_noop(self) -> None:
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_prepared()
        mtimes = {str(path): path.stat().st_mtime_ns for path, _ in self.paths()}
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(mtimes, {str(path): path.stat().st_mtime_ns for path, _ in self.paths()})
        for dependency in MANIFEST['dependencies']:
            self.assertFalse((self.root / dependency['path'] / '.git/index').exists())

    def test_partly_prepared_dependency_is_completed(self) -> None:
        dependency = MANIFEST['dependencies'][0]
        item = dependency['files'][0]
        destination = self.root / dependency['path'] / item['path']
        destination.write_bytes((ROOT / dependency['path'] / item['path']).read_bytes())
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_prepared()

    def test_unknown_local_edit_rejects_before_any_patch(self) -> None:
        dependency = MANIFEST['dependencies'][-1]
        item = dependency['files'][0]
        path = self.root / dependency['path'] / item['path']
        path.write_bytes(path.read_bytes() + b'\n// Preserve this unrelated local edit.\n')
        before = self.snapshot()
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Local dependency changes differ', result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_crlf_checkout_is_supported(self) -> None:
        for path, _ in self.paths():
            if path.exists():
                path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assert_prepared()

    def test_unrelated_file_is_preserved(self) -> None:
        marker = self.root / MANIFEST['dependencies'][0]['path'] / 'local-only.log'
        marker.write_bytes(b'not part of the dependency patch\n')
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(marker.read_bytes(), b'not part of the dependency patch\n')

    def test_patch_checksum_mismatch_rejects_without_changes(self) -> None:
        dependency = MANIFEST['dependencies'][0]
        patch = self.cmake / 'dependency-patches' / dependency['patch']
        patch.write_bytes(patch.read_bytes() + b'\n')
        before = self.snapshot()
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('checksum mismatch', result.stderr)
        self.assertEqual(before, self.snapshot())

    def test_symlink_is_rejected(self) -> None:
        dependency = MANIFEST['dependencies'][0]
        item = dependency['files'][0]
        path = self.root / dependency['path'] / item['path']
        target = Path(self.temp.name) / 'outside-source'
        target.write_bytes(path.read_bytes())
        original = target.read_bytes()
        path.unlink()
        try:
            path.symlink_to(target)
        except OSError as error:
            self.skipTest(str(error))
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('expects a regular file', result.stderr)
        self.assertEqual(target.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
