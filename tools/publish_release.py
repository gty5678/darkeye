#!/usr/bin/env python3
"""Publish native distributions to GitHub and updater payloads to Cloudflare R2."""

import argparse
import json
import os
from pathlib import Path
import subprocess

from package import file_sha256, project_version


TARGETS = {"windows-x64": ("portable.7z", "setup.exe"),
           "macos-arm64": ("portable.zip", "setup.dmg")}


def collect(root, version):
    """Validate all artifacts before making any remote changes."""
    assets, updates = [], []
    for target, suffixes in TARGETS.items():
        directory = root / f"Darkeye-{target}"
        base = f"Darkeye-{version}-{target}"
        distributions = [directory / f"{base}-{suffix}" for suffix in suffixes]
        archive = directory / f"{base}-update.tar.zst"
        manifest_path = directory / "latest.json"
        for path in [*distributions, archive, manifest_path]:
            if not path.is_file() or not path.stat().st_size:
                raise RuntimeError(f"Missing or empty artifact: {path}")
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        expected_url = f"https://darkeye.win/releases/{archive.name}"
        if (manifest.get("latestVersion") != version
                or set(manifest.get("packages", {})) != {target}
                or manifest["packages"][target].get("url") != expected_url
                or manifest["packages"][target].get("sha256") != file_sha256(archive)):
            raise RuntimeError(f"Invalid update manifest: {manifest_path}")
        assets.extend(distributions)
        updates.append((target, archive, manifest_path))
    return assets, updates


def gh(*args, **kwargs):
    return subprocess.run(["gh", *args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    version = project_version()
    tag = f"v{version}"
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/") and ref != f"refs/tags/{tag}":
        raise RuntimeError(f"Tag {ref} does not match project version {tag}")
    assets, updates = collect(args.artifacts, version)
    print(f"GitHub Release {tag}: " + ", ".join(p.name for p in assets), flush=True)
    for target, archive, manifest in updates:
        print(f"R2: {archive} -> releases/{archive.name}", flush=True)
        print(f"R2: {manifest} -> updates/stable/{target}/latest.json", flush=True)
    if args.dry_run:
        return

    required = ("R2_ENDPOINT_URL", "R2_BUCKET", "AWS_ACCESS_KEY_ID",
                "AWS_SECRET_ACCESS_KEY", "GH_TOKEN", "GH_REPO", "GITHUB_SHA")
    missing = [key for key in required if not os.environ.get(key)]
    if missing:
        raise RuntimeError("Missing configuration: " + ", ".join(missing))
    import boto3
    s3 = boto3.client("s3", endpoint_url=os.environ["R2_ENDPOINT_URL"], region_name="auto")
    bucket = os.environ["R2_BUCKET"]
    # Verify access before creating a release.
    s3.head_bucket(Bucket=bucket)
    existing = subprocess.run(["gh", "release", "view", tag, "--json", "isDraft"],
                              capture_output=True, text=True)
    # Existing tags must identify exactly the source used for these packages.
    tagged = subprocess.run(
        ["gh", "api", f"repos/{os.environ['GH_REPO']}/commits/{tag}", "--jq", ".sha"],
        capture_output=True, text=True)
    if tagged.returncode == 0:
        if tagged.stdout.strip() != os.environ["GITHUB_SHA"]:
            raise RuntimeError(f"Tag {tag} belongs to another commit; bump the project version")
    elif "404" not in tagged.stderr and "422" not in tagged.stderr:
        raise RuntimeError(f"Cannot check release tag: {tagged.stderr}")
    if existing.returncode == 0:
        if not json.loads(existing.stdout)["isDraft"]:
            raise RuntimeError(f"Release {tag} is already published; bump the version or repair R2 manually")
    else:
        gh("release", "create", tag, "--target", os.environ["GITHUB_SHA"],
           "--title", tag, "--generate-notes", "--draft")
    gh("release", "upload", tag, *map(str, assets), "--clobber")
    # Upload both payloads before advancing either platform's update pointer.
    for target, archive, manifest in updates:
        s3.upload_file(str(archive), bucket, f"releases/{archive.name}",
                       ExtraArgs={"ContentType": "application/zstd",
                                  "CacheControl": "public, max-age=31536000, immutable"})
    for target, archive, manifest in updates:
        s3.upload_file(str(manifest), bucket, f"updates/stable/{target}/latest.json",
                       ExtraArgs={"ContentType": "application/json",
                                  "CacheControl": "no-cache, max-age=0, must-revalidate"})

    gh("release", "edit", tag, "--draft=false", "--latest")


if __name__ == "__main__":
    main()
