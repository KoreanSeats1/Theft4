#!/usr/bin/env python3
"""Exercise generator routing with fake dependencies/CMake; no app/device build."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
LIBRARIES = ('libMoltenVK.a', 'libMoltenVK_ShaderConverter.a',
             'libMoltenVK_Common.a', 'libspirv-cross.a', 'libSPIRV-Tools.a')


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='theft4 generator ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        shutil.copy2(ROOT / 'Generate-Theft4-Xcode.command', self.root)
        (self.root / 'tools').mkdir()
        (self.root / 'tools/setup_repo.py').write_text(
            "from pathlib import Path\nPath('setup-called').touch()\n")
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.make_tool('cmake', """
import json, pathlib, sys
root = pathlib.Path.cwd()
args = sys.argv[1:]
(root / 'configure.json').write_text(json.dumps(args))
preset = args[args.index('--preset') + 1]
(root / 'out/build' / preset / 'LibertyRecomp-ALL.xcodeproj').mkdir(parents=True)
""")
        self.make_tool('open', """
import pathlib, sys
pathlib.Path('opened.txt').write_text(sys.argv[1])
""")
        developer = self.root / 'Xcode/Contents/Developer'
        (developer / 'usr/bin').mkdir(parents=True)
        xcodebuild = developer / 'usr/bin/xcodebuild'
        xcodebuild.write_text('#!/bin/sh\nexit 0\n')
        xcodebuild.chmod(0o755)
        self.libs = self.root / 'graphics Release'
        self.libs.mkdir()
        for name in LIBRARIES:
            (self.libs / name).touch()
        self.env = dict(os.environ)
        for key in ('THEFT4_BUILD_CONFIGURATION', 'THEFT4_DEVELOPMENT_TEAM',
                    'THEFT4_XENIOS_ROOT'):
            self.env.pop(key, None)
        self.env.update(PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        DEVELOPER_DIR=str(developer),
                        THEFT4_MOLTENVK_IOS_LIB_DIR=str(self.libs))

    def make_tool(self, name, body):
        path = self.bin / name
        path.write_text('#!' + sys.executable + '\n' + body)
        path.chmod(0o755)

    def run_generator(self, *args):
        return subprocess.run(['/bin/zsh', str(self.root / 'Generate-Theft4-Xcode.command'),
                               *args], cwd=self.root, env=self.env,
                              capture_output=True, text=True, timeout=15)

    def arguments(self):
        return json.loads((self.root / 'configure.json').read_text())

    def test_default_release_signed_and_opened(self):
        result = self.run_generator('TESTTEAM')
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.arguments()
        self.assertEqual(args[:2], ['--preset', 'ios-device-release'])
        for arg in ('-DTHEFT4_SIGN_DEVICE=ON', '-DTHEFT4_BUILD_GAME_CODE=ON',
                    '-DTHEFT4_COMPILE_GTA4_NATIVE_BACKEND=ON',
                    '-DTHEFT4_ENABLE_GTA4_NATIVE_BACKEND=ON',
                    '-DTHEFT4_ENABLE_GAME_STARTUP=ON', '-DREXGLUE_HEADLESS_KERNEL=ON',
                    '-DREXGLUE_RUNTIME_ONLY=ON', '-DLIBERTY_IOS_DEVELOPMENT_TEAM=TESTTEAM'):
            self.assertIn(arg, args)
        self.assertIn('ios-device-release/', (self.root / 'opened.txt').read_text())
        self.assertIn('Debug executable', result.stdout)

    def test_explicit_debug(self):
        self.env['THEFT4_BUILD_CONFIGURATION'] = 'Debug'
        result = self.run_generator('TESTTEAM')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.arguments()[:2], ['--preset', 'ios-device-debug'])
        self.assertIn('ios-device-debug/', (self.root / 'opened.txt').read_text())

    def test_invalid_configuration_before_setup(self):
        self.env['THEFT4_BUILD_CONFIGURATION'] = 'Bogus'
        result = self.run_generator()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('must be Release or Debug', result.stderr)
        self.assertFalse((self.root / 'setup-called').exists())

    def test_unsigned_without_team(self):
        result = self.run_generator()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('-DTHEFT4_SIGN_DEVICE=OFF', self.arguments())
        self.assertIn('unsigned', result.stdout)

    def test_missing_graphics_does_not_configure(self):
        (self.libs / LIBRARIES[0]).unlink()
        result = self.run_generator('TESTTEAM')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Missing graphics archive', result.stderr)
        self.assertFalse((self.root / 'configure.json').exists())


if __name__ == '__main__':
    unittest.main()
