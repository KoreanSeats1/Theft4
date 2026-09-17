#!/usr/bin/env python3
"""Exercise the safeguards that keep Lab out of the working app and its files."""
import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("theft4_lab", ROOT / "tools/theft4_lab.py")
lab = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lab)


class IsolationTests(unittest.TestCase):
    def setUp(self):
        self.info = {"CFBundleIdentifier": lab.LAB_ID, "CFBundleDisplayName": lab.LAB_NAME,
                     "CFBundleExecutable": "Theft4", "Theft4LabBuild": True}
        self.entitlements = {"application-identifier": "TEAM." + lab.LAB_ID,
                             "com.apple.developer.team-identifier": "TEAM",
                             "keychain-access-groups": ["TEAM." + lab.LAB_ID]}

    def test_isolated_signed_identity_is_accepted(self):
        lab.verify_identity(self.info, self.entitlements, "TEAM")

    def test_ordinary_or_disguised_app_is_rejected(self):
        for key, value in (("CFBundleIdentifier", lab.STABLE_ID),
                           ("CFBundleDisplayName", "Theft4"),
                           ("Theft4LabBuild", False),
                           ("CFBundleExecutable", "../Theft4")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                lab.verify_identity(dict(self.info, **{key: value}), self.entitlements, "TEAM")

    def test_stable_entitlement_or_wrong_team_is_rejected(self):
        for key, value in (("application-identifier", "TEAM." + lab.STABLE_ID),
                           ("com.apple.developer.team-identifier", "OTHER"),
                           ("keychain-access-groups", ["TEAM." + lab.STABLE_ID])):
            with self.subTest(key=key), self.assertRaises(ValueError):
                lab.verify_identity(self.info, dict(self.entitlements, **{key: value}), "TEAM")

    def test_shared_data_entitlements_are_rejected(self):
        for key in ("com.apple.security.application-groups",
                    "com.apple.developer.icloud-container-identifiers",
                    "com.apple.developer.ubiquity-container-identifiers",
                    "com.apple.developer.ubiquity-kvstore-identifier",
                    "com.apple.developer.app-migration.data-container-access"):
            with self.subTest(key=key), self.assertRaises(ValueError):
                lab.verify_identity(self.info, dict(self.entitlements, **{key: ["shared"]}), "TEAM")

    def test_wrong_app_stops_before_any_subprocess(self):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory)
            (app / "Info.plist").write_bytes(plistlib.dumps(dict(self.info, CFBundleIdentifier=lab.STABLE_ID)))
            with patch.object(lab.subprocess, "run") as run, self.assertRaises(ValueError):
                lab.verify_app(app, "TEAM")
            run.assert_not_called()

    def test_output_symlink_cannot_escape_lane(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "lane").mkdir()
            (root / "stable").mkdir()
            (root / "lane/build").symlink_to(root / "stable", target_is_directory=True)
            with self.assertRaises(ValueError):
                lab.inside(root / "lane/build/output.app", root / "lane")

    def test_source_symlink_cannot_reference_working_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "lane").mkdir()
            (root / "stable.cpp").write_text("working code")
            (root / "lane/code.cpp").symlink_to(root / "stable.cpp")
            with self.assertRaises(ValueError):
                lab.tree_manifest(root / "lane")

    def test_manifest_detects_changed_content_and_deleted_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "code.cpp").write_text("A")
            first = lab.manifest_hash(lab.tree_manifest(root))
            (root / "code.cpp").write_text("B")
            self.assertNotEqual(first, lab.manifest_hash(lab.tree_manifest(root)))
            (root / "code.cpp").unlink()
            self.assertNotEqual(first, lab.manifest_hash(lab.tree_manifest(root)))

    def test_git_metadata_is_stripped_only_from_private_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "copy/.git").mkdir(parents=True)
            (root / "copy/.git/HEAD").write_text("private copy")
            (root / "copy/dependency").mkdir()
            (root / "copy/dependency/.git").write_text("gitdir: ../../../working/.git")
            (root / "working/.git").mkdir(parents=True)
            (root / "working/.git/HEAD").write_text("preserve")
            lab.strip_git_metadata(root / "copy")
            self.assertEqual((root / "working/.git/HEAD").read_text(), "preserve")
            self.assertFalse((root / "copy/.git").exists())
            self.assertFalse((root / "copy/dependency/.git").exists())
            lab.tree_manifest(root / "copy")

    def test_archive_rebuild_does_not_hide_real_source_edits(self):
        with tempfile.TemporaryDirectory() as directory:
            lane = Path(directory)
            source = lane / 'source'
            output = source / lab.SOURCE_OUTPUTS[0]
            output.mkdir(parents=True)
            (source / 'renderer.cpp').write_text('baseline')
            (output / 'librexruntime.a').write_text('old build product')
            manifest = lab.tree_manifest(source)  # First-generation receipt.
            (lane / 'source-manifest.json').write_text(json.dumps(manifest))
            state = {'source_tree_sha256': lab.manifest_hash(manifest)}
            (output / 'librexruntime.a').write_text('new independent build product')
            lab.verify_source_snapshot(lane, state)
            (source / 'renderer.cpp').write_text('unrecorded edit')
            with self.assertRaisesRegex(ValueError, 'Generated source changed'):
                lab.verify_source_snapshot(lane, state)

    def test_main_and_detached_head_cannot_prepare(self):
        for branch in ("main", "master", ""):
            with self.subTest(branch=branch), patch.object(lab, "git", return_value=branch):
                with self.assertRaises(ValueError):
                    lab.require_experiment()

    def test_uncommitted_experiment_cannot_be_built(self):
        with patch.object(lab, "git", side_effect=["codex/test", " M ios/CMakeLists.txt"]):
            with self.assertRaises(ValueError):
                lab.require_experiment()


if __name__ == "__main__":
    unittest.main()
