#!/usr/bin/env python3
"""Regression coverage for the unoptimized Release build that stalled iOS."""

from pathlib import Path
import tempfile
import unittest

from verify_release_build import TARGETS, check_arguments, verify


class ReleaseBuildTests(unittest.TestCase):
    def test_actual_broken_flags_are_rejected(self):
        with self.assertRaises(ValueError):
            check_arguments("'-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG' "
                            "-fpascal-strings -O0 '-DCMAKE_INTDIR=Release-iphoneos'")

    def test_release_name_does_not_substitute_for_ndebug(self):
        with self.assertRaises(ValueError):
            check_arguments("-O3 '-DREXGLUE_BUILD_CONFIG=Release'")

    def test_last_option_wins(self):
        for flags in ("-O3 -DNDEBUG -O0", "-O3 -DNDEBUG -UNDEBUG"):
            with self.assertRaises(ValueError):
                check_arguments(flags)
        check_arguments("-O0 -UNDEBUG -O3 -DNDEBUG")

    def test_missing_native_target_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in TARGETS[:-1]:
                self.write_response(root, target)
            with self.assertRaisesRegex(ValueError, TARGETS[-1]):
                verify(root)

    def test_all_gameplay_targets_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in TARGETS:
                self.write_response(root, target)
            self.assertEqual(verify(root), len(TARGETS))
            self.write_response(root, "theft4_core_bridge", "-O0")
            with self.assertRaisesRegex(ValueError, "theft4_core_bridge"):
                verify(root)

    @staticmethod
    def write_response(root, target, flags="-O3 -DNDEBUG"):
        directory = (root / "build" / f"{target}.build" / "Release-iphoneos" /
                     "Objects-normal" / "arm64")
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "test-common-args.resp").write_text(flags)


if __name__ == "__main__":
    unittest.main()
