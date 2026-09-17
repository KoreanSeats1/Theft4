#!/usr/bin/env python3
"""Build and install an isolated M5 Lab app from a committed experiment.

All generated files live in this checkout's out/m5-lab. The frozen baseline is
read-only input. This tool never installs, launches, removes or writes data to
the ordinary Theft4 bundle identifier.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
LAB_ID = "com.theft4.m5lab"
STABLE_ID = "com.theft4.bringup"
LAB_NAME = "Theft4 Lab"
BASE_COMMIT = "814905f9"


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def inside(path, parent):
    path, parent = Path(path).resolve(), Path(parent).resolve()
    if path == parent or parent not in path.parents:
        raise ValueError(f"Expected a private child of {parent}: {path}")
    return path


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args]).decode().strip()


def require_experiment():
    branch = git("branch", "--show-current")
    if not branch or branch in ("main", "master"):
        raise ValueError("Use a named experiment branch in a separate worktree, not main/master")
    if git("status", "--porcelain=1", "--untracked-files=no"):
        raise ValueError("Commit tracked changes before preparing/building a Lab artifact")
    return git("rev-parse", "HEAD")


def lane_path():
    return inside(ROOT / "out/m5-lab", ROOT)


def tree_manifest(root):
    """Hash regular files and symlink text, rejecting any link out of the copy."""
    root = Path(root).resolve()
    result = {}
    for parent, dirs, files in os.walk(root):
        if ".git" in dirs or ".git" in files:
            raise ValueError(f"Build snapshot must not contain Git metadata: {parent}")
        for name in sorted(dirs + files):
            path = Path(parent) / name
            rel = path.relative_to(root).as_posix()
            if path.is_symlink():
                inside(path, root)
                result[rel] = {"symlink": os.readlink(path)}
            elif path.is_file():
                result[rel] = {"sha256": sha(path), "size": path.stat().st_size}
    return result


def manifest_hash(manifest):
    return hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()


def clone_directory(source, destination):
    if destination.exists() or destination.is_symlink():
        raise ValueError(f"Refusing to overwrite an existing snapshot: {destination}")
    # APFS copy-on-write clones are independent files, never hard links.
    subprocess.run(["/bin/cp", "-cR", str(source), str(destination)], check=True)


def strip_git_metadata(root):
    # A frozen source export may contain repository metadata. Only remove it
    # from the newly created private copy; never follow directory symlinks.
    for parent, dirs, files in os.walk(root):
        if ".git" in dirs:
            path = Path(parent) / ".git"
            if path.is_symlink():
                path.unlink()
            else:
                shutil.rmtree(path)
            dirs.remove(".git")
        if ".git" in files:
            (Path(parent) / ".git").unlink()


def run(command, log=None, cwd=None):
    print("Running: " + str(command[0]) + " " + " ".join(map(str, command[1:5])), flush=True)
    env = dict(os.environ)
    env.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    if Path(command[0]).name == "cmake":
        env["PATH"] = str(Path(command[0]).resolve().parent) + os.pathsep + env.get("PATH", "")
    if log:
        with Path(log).open("w") as output:
            result = subprocess.run(list(map(str, command)), cwd=cwd, env=env,
                                    stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            print("\n".join(Path(log).read_text(errors="replace").splitlines()[-45:]), file=sys.stderr)
            raise ValueError(f"Command failed ({result.returncode}); full log: {log}")
    else:
        subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)


def tracked_files(revision):
    data = subprocess.check_output(["git", "-C", str(ROOT), "ls-tree", "-rz", revision])
    result = {}
    for entry in data.split(b"\0"):
        if entry:
            meta, name = entry.split(b"\t", 1)
            mode, kind, object_id = meta.decode().split()
            result[name.decode()] = (mode, object_id)
    return result


def prepare(args):
    commit = require_experiment()
    lane = lane_path()
    baseline = args.baseline.resolve()
    if baseline == ROOT or ROOT in baseline.parents or lane in baseline.parents:
        raise ValueError("Frozen baseline must be outside the experimental checkout")
    identity_path = baseline / "identity.json"
    identity = json.loads(identity_path.read_text())
    if identity["bundle_id"] != STABLE_ID or identity["compiler_tune"] != "apple-m5":
        raise ValueError("Expected the preserved ordinary M5 baseline identity")
    preserved_app = baseline / "artifacts/known-good/Theft4.app/Theft4"
    if sha(preserved_app) != identity["baseline_executable_sha256"]:
        raise ValueError("Preserved M5 executable no longer matches its identity")
    for name, expected in identity["dependencies"].items():
        if sha(baseline / "dependencies/lib" / name) != expected:
            raise ValueError(f"Frozen dependency hash mismatch: {name}")
    lane.mkdir(parents=True, exist_ok=True)
    state_file = lane / "state.json"
    if state_file.exists():
        state = json.loads(state_file.read_text())
        if state["baseline"] != str(baseline) or state["baseline_identity_sha256"] != sha(identity_path):
            raise ValueError("Existing Lab lane belongs to a different frozen baseline")
        # Detect edits to the generated snapshot; author changes in the worktree.
        if manifest_hash(tree_manifest(lane / "source")) != state["source_tree_sha256"]:
            raise ValueError("Generated source changed; preserve those edits before recreating the Lab lane")
        if manifest_hash(tree_manifest(lane / "dependencies")) != state["dependency_tree_sha256"]:
            raise ValueError("Private dependencies changed; preserve them as a separately reviewed experiment")
    else:
        print("Cloning frozen source and dependencies into private Lab files...", flush=True)
        clone_directory(baseline / "source", inside(lane / "source", lane))
        clone_directory(baseline / "dependencies", inside(lane / "dependencies", lane))
        strip_git_metadata(lane / "source")
        strip_git_metadata(lane / "dependencies")
        state = {"baseline": str(baseline), "baseline_identity_sha256": sha(identity_path)}
    before = tracked_files(args.base_commit)
    after = tracked_files(commit)
    for rel, (mode, object_id) in after.items():
        if mode == "160000" and before.get(rel) != (mode, object_id):
            raise ValueError(f"Dependency revision changed: {rel}; prepare a separately reviewed dependency snapshot")
    previous = set(state.get("overlay_paths", [])) | set(before)
    for rel in sorted(previous - set(after)):
        destination = lane / "source" / rel
        if before.get(rel, (None,))[0] == "160000":
            raise ValueError(f"Dependency removed: {rel}; prepare a new dependency snapshot")
        inside(destination.parent, lane)
        if destination.is_file() or destination.is_symlink():
            destination.unlink()
    for rel, (mode, _) in after.items():
        if mode == "160000":
            continue  # Frozen private dependency sources, including local patches.
        source, destination = ROOT / rel, lane / "source" / rel
        inside(destination.parent, lane)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink():
            destination.unlink()
        if source.is_symlink():
            destination.unlink(missing_ok=True)
            destination.symlink_to(os.readlink(source))
            inside(destination, lane / "source")
        else:
            shutil.copy2(source, destination)
    source_manifest = tree_manifest(lane / "source")
    dependency_manifest = tree_manifest(lane / "dependencies")
    write_json(lane / "source-manifest.json", source_manifest)
    write_json(lane / "dependency-manifest.json", dependency_manifest)
    state.update(source_commit=commit, base_commit=git("rev-parse", args.base_commit),
                 overlay_paths=sorted(after), team=args.team, bundle_id=LAB_ID,
                 display_name=LAB_NAME, source_tree_sha256=manifest_hash(source_manifest),
                 dependency_tree_sha256=manifest_hash(dependency_manifest),
                 prepared_at=datetime.now(timezone.utc).isoformat())
    write_json(state_file, state)
    cmake = args.cmake or shutil.which("cmake")
    if not cmake:
        raise ValueError("Supply --cmake /path/to/cmake (3.29 or newer)")
    command = [cmake, "--preset", "ios-device-release", "-B", str(lane / "build"),
               "-DREXGLUE_RUNTIME_ONLY=ON", "-DREXGLUE_HEADLESS_KERNEL=ON",
               "-DTHEFT4_BUILD_GAME_CODE=ON", "-DTHEFT4_ENABLE_GAME_STARTUP=ON",
               "-DTHEFT4_COMPILE_GTA4_NATIVE_BACKEND=ON", "-DTHEFT4_ENABLE_GTA4_NATIVE_BACKEND=ON",
               "-DTHEFT4_ENABLE_XENIOS_DIRECT_RESOLVE=OFF", "-DTHEFT4_ENABLE_THIN_LTO=OFF",
               "-DTHEFT4_ARM64_TUNE=apple-m5", "-DTHEFT4_PUBLIC_BUILD=ON",
               f"-DTHEFT4_BUNDLE_IDENTIFIER={LAB_ID}", f"-DTHEFT4_DISPLAY_NAME={LAB_NAME}",
               "-DTHEFT4_LAB_BUILD=ON", "-DTHEFT4_SIGN_DEVICE=ON",
               f"-DLIBERTY_IOS_DEVELOPMENT_TEAM={args.team}", "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0",
               f"-DTHEFT4_XENIOS_IOS_LIB_DIR={lane / 'dependencies/lib'}",
               f"-DTHEFT4_XENIOS_GENERATED_SHADER_DIR={lane / 'dependencies/generated'}"]
    write_json(lane / "configure-command.json", command)
    run(command, lane / "configure.log", lane / "source")
    state["cmake"] = str(cmake)
    write_json(state_file, state)
    print(f"Prepared {LAB_NAME} from {commit[:12]} in {lane}", flush=True)


def read_state():
    lane = lane_path()
    state = json.loads((lane / "state.json").read_text())
    if state["bundle_id"] != LAB_ID or state["display_name"] != LAB_NAME:
        raise ValueError("Invalid Lab lane identity")
    for path in ("source", "dependencies", "build"):
        inside(lane / path, lane)
    return lane, state


def verify_identity(info, entitlements, team):
    if (info.get("CFBundleIdentifier") != LAB_ID or
            info.get("CFBundleDisplayName") != LAB_NAME or
            info.get("Theft4LabBuild") is not True or
            info.get("CFBundleExecutable") != "Theft4"):
        raise ValueError("Refusing an app that is not explicitly identified as Theft4 Lab")
    app_identifier = f"{team}.{LAB_ID}"
    if entitlements.get("application-identifier") != app_identifier:
        raise ValueError("Signed application identifier does not match Lab")
    if entitlements.get("com.apple.developer.team-identifier") != team:
        raise ValueError("Unexpected signing team")
    for key in ("com.apple.security.application-groups", "com.apple.developer.icloud-container-identifiers",
                "com.apple.developer.ubiquity-container-identifiers",
                "com.apple.developer.ubiquity-kvstore-identifier",
                "com.apple.developer.app-migration.data-container-access"):
        if entitlements.get(key):
            raise ValueError(f"Lab must not share containers: {key}")
    if any(group != app_identifier for group in entitlements.get("keychain-access-groups", [])):
        raise ValueError("Lab must not share keychain access groups")


def verify_app(app, team):
    info = plistlib.loads((app / "Info.plist").read_bytes())
    # Check plain identity before invoking signing tools or any device command.
    if info.get("CFBundleIdentifier") != LAB_ID:
        raise ValueError("Refusing to process the ordinary Theft4 app")
    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", app])
    result = subprocess.run(["/usr/bin/codesign", "-d", "--entitlements", "-", str(app)],
                            capture_output=True, check=True)
    entitlements = plistlib.loads(result.stdout)
    verify_identity(info, entitlements, team)
    return {"bundle_id": LAB_ID, "display_name": LAB_NAME, "team": team,
            "executable_sha256": sha(app / "Theft4"),
            "app_tree_sha256": manifest_hash(tree_manifest(app))}


def build(args):
    commit = require_experiment()
    lane, state = read_state()
    if commit != state["source_commit"]:
        raise ValueError("Commit changed; run prepare again before building")
    for name, key in (("source", "source_tree_sha256"), ("dependencies", "dependency_tree_sha256")):
        if manifest_hash(tree_manifest(lane / name)) != state[key]:
            raise ValueError(f"Private {name} snapshot changed since preparation")
    command = [state["cmake"], "--build", str(lane / "build"), "--config", "Release",
               "--target", "Theft4", "--parallel", str(args.jobs), "--", "-allowProvisioningUpdates"]
    write_json(lane / "build-command.json", command)
    run(command, lane / "build.log")
    run([sys.executable, ROOT / "tests/ios/verify_release_build.py", lane / "build"],
        lane / "release-verification.log")
    app = lane / "build/theft4/Release/Theft4.app"
    result = verify_app(app, state["team"])
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    artifact = inside(lane / "artifacts" / f"{commit[:12]}-{stamp}", lane)
    artifact.mkdir(parents=True)
    clone_directory(app, artifact / "Theft4.app")
    result.update(source_commit=commit, base_commit=state["base_commit"],
                  source_tree_sha256=state["source_tree_sha256"],
                  dependency_tree_sha256=state["dependency_tree_sha256"],
                  built_at=stamp, app=str(artifact / "Theft4.app"),
                  gameplay_acceptance="not tested")
    write_json(artifact / "receipt.json", result)
    write_json(lane / "latest-artifact.json", result)
    print(json.dumps(result, indent=2), flush=True)


def verified_artifact():
    lane, state = read_state()
    receipt = json.loads((lane / "latest-artifact.json").read_text())
    app = inside(receipt["app"], lane / "artifacts")
    checked = verify_app(app, state["team"])
    for key, value in checked.items():
        if receipt.get(key) != value:
            raise ValueError(f"Artifact changed since build: {key}")
    return lane, app, receipt


def install(args):
    lane, app, receipt = verified_artifact()
    run(["xcrun", "devicectl", "device", "install", "app", "--device", args.device,
         "--json-output", lane / "install-result.json", app], lane / "install.log")
    print(f"Installed {LAB_ID} from {receipt['source_commit'][:12]}; ordinary Theft4 was not targeted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("prepare", help="Copy private inputs and configure Lab")
    p.add_argument("--baseline", type=Path, required=True)
    p.add_argument("--team", required=True)
    p.add_argument("--base-commit", default=BASE_COMMIT)
    p.add_argument("--cmake")
    p.set_defaults(action=prepare)
    p = commands.add_parser("build", help="Build, verify and archive a signed Release app")
    p.add_argument("--jobs", type=int, choices=range(1, 17), default=4)
    p.set_defaults(action=build)
    p = commands.add_parser("verify", help="Verify the archived app without touching a device")
    p.set_defaults(action=lambda args: print(json.dumps(verified_artifact()[2], indent=2)))
    p = commands.add_parser("install", help="Install only the verified Lab identity")
    p.add_argument("--device", required=True)
    p.set_defaults(action=install)
    args = parser.parse_args()
    try:
        args.action(args)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Lab operation stopped: {error}\n")


if __name__ == "__main__":
    main()
