#!/usr/bin/env python3
"""Publish three already-validated SDK ZIPs; a CI helper, not a library CLI."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ARCHIVE = re.compile(
    r"xlog-codec-([0-9]+\.[0-9]+\.[0-9]+)-(macos-arm64|windows-x64-mingw|linux-x64)\.zip")
PLATFORMS = {"macos-arm64", "windows-x64-mingw", "linux-x64"}


def gh(*arguments, missing=False, output=None):
    result = subprocess.run(
        ["gh", *arguments], env=os.environ.copy(),
        stdout=output if output is not None else subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, check=False)
    if result.returncode:
        if missing and re.search(r"\(HTTP 404\)(?:\s|$)", result.stderr):
            return None
        # Never print the process environment or raw server diagnostics.
        status = re.search(r"\(HTTP ([0-9]{3})\)", result.stderr)
        detail = f"HTTP {status.group(1)}" if status else f"exit {result.returncode}"
        raise RuntimeError(f"gh {' '.join(arguments[:2])} failed ({detail})")
    return result.stdout.strip() if output is None else None


def api(endpoint, *arguments, missing=False):
    result = gh("api", endpoint, *arguments, missing=missing)
    return json.loads(result) if result is not None else None


def find_release(repository, tag):
    owner, name = repository.split("/")
    query = ("query($owner: String!, $name: String!, $tag: String!) {"
             " repository(owner: $owner, name: $name) { release(tagName: $tag) { databaseId } } }")
    response = api("graphql", "-f", f"query={query}", "-f", f"owner={owner}",
                   "-f", f"name={name}", "-f", f"tag={tag}")
    if response.get("errors") or not isinstance(response.get("data"), dict):
        raise RuntimeError("GraphQL release lookup failed")
    result = response["data"].get("repository")
    if not isinstance(result, dict) or "release" not in result:
        raise RuntimeError("GraphQL repository lookup failed")
    if result["release"] is None:
        return None
    release = result["release"]
    release_id = release.get("databaseId") if isinstance(release, dict) else None
    if type(release_id) is not int or release_id <= 0:
        raise RuntimeError("GraphQL returned an invalid release ID")
    return api(f"repos/{repository}/releases/{release_id}")


def verify_assets(release, files, repository, *, complete):
    assets = release["assets"]
    names = [asset["name"] for asset in assets]
    if len(names) != len(set(names)) or set(names) - files.keys():
        raise RuntimeError("Release contains duplicate or unexpected assets")
    if complete and set(names) != files.keys():
        raise RuntimeError("Release does not contain the complete three ZIPs")
    for asset in assets:
        path = files[asset["name"]]
        expected = hashlib.sha256(path.read_bytes()).hexdigest()
        if asset["state"] != "uploaded" or asset["size"] != path.stat().st_size:
            raise RuntimeError(f"Incomplete or different asset: {path.name}")
        digest = asset.get("digest")
        if not digest:
            with tempfile.TemporaryFile() as downloaded:
                gh("api", f"repos/{repository}/releases/assets/{asset['id']}",
                   "-H", "Accept: application/octet-stream", output=downloaded)
                downloaded.seek(0)
                digest = "sha256:" + hashlib.sha256(downloaded.read()).hexdigest()
        if digest != "sha256:" + expected:
            raise RuntimeError(f"Asset SHA-256 differs: {path.name}")
    return set(names)


def publish(directory):
    paths = sorted(Path(directory).iterdir())
    matches = [ARCHIVE.fullmatch(path.name) for path in paths]
    if (len(paths) != 3 or not all(matches)
            or any(not path.is_file() or path.is_symlink() for path in paths)
            or {match[2] for match in matches} != PLATFORMS
            or len({match[1] for match in matches}) != 1):
        raise RuntimeError("Expected exactly three platform ZIPs with one numeric major.minor.patch version")
    files = {path.name: path.resolve() for path in paths}
    version = matches[0][1]
    values = {}
    for key, pattern in {
        "GITHUB_REPOSITORY": r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",
        "GITHUB_SHA": r"[0-9a-fA-F]{40}",
        "GITHUB_RUN_NUMBER": r"[1-9][0-9]*",
        "GITHUB_RUN_ID": r"[1-9][0-9]*",
    }.items():
        values[key] = os.environ.get(key, "")
        if not re.fullmatch(pattern, values[key]):
            raise RuntimeError(f"Missing or invalid {key}")
    repository, sha = values["GITHUB_REPOSITORY"], values["GITHUB_SHA"]
    tag = f"v{version}-build.{values['GITHUB_RUN_NUMBER']}"
    commit_endpoint = f"repos/{repository}/commits/tags/{tag}"
    release = find_release(repository, tag)
    # Missing commits can return 422; only the exact ref endpoint's 404 means absent.
    if api(f"repos/{repository}/git/ref/tags/{tag}", missing=True) is None:
        gh("api", f"repos/{repository}/git/refs", "--method", "POST",
           "-f", f"ref=refs/tags/{tag}", "-f", f"sha={sha}")
    actual_sha = gh("api", commit_endpoint, "--jq", ".sha")
    if actual_sha.lower() != sha.lower():
        raise RuntimeError(f"Existing tag {tag} points to a different SHA")
    if release is None:
        notes = (f"SDK version: {version}\nCommit: {sha}\n\n"
                 f"Workflow run: https://github.com/{repository}/actions/runs/{values['GITHUB_RUN_ID']}\n")
        # Consume the creation response: list/lookup reads may lag a successful write.
        release = api(f"repos/{repository}/releases", "--method", "POST",
                      "-f", f"tag_name={tag}", "-f", f"target_commitish={sha}",
                      "-f", f"name=xlog-codec {version} (build {values['GITHUB_RUN_NUMBER']})",
                      "-f", f"body={notes}", "-F", "draft=true", "-F", "prerelease=false")
    release_endpoint = f"repos/{repository}/releases/{release['id']}"
    if release["tag_name"] != tag:
        raise RuntimeError("GitHub returned an unexpected release tag")
    if release["draft"]:
        present = verify_assets(release, files, repository, complete=False)
        missing = [str(path) for name, path in files.items() if name not in present]
        if missing:
            gh("release", "upload", tag, *missing, "--repo", repository)
        release = api(release_endpoint)
        verify_assets(release, files, repository, complete=True)
        # Recheck the actual tag after upload; never rely on target_commitish.
        if gh("api", commit_endpoint, "--jq", ".sha").lower() != sha.lower():
            raise RuntimeError("Tag SHA changed before publishing")
        gh("release", "edit", tag, "--repo", repository,
           "--draft=false", "--prerelease=false", "--latest")
        release = api(release_endpoint)
        if release["draft"] or release["prerelease"]:
            raise RuntimeError("Release did not become a published ordinary release")
    else:
        verify_assets(release, files, repository, complete=True)
        if release["prerelease"]:
            raise RuntimeError("Existing release is a prerelease")
    url = release["html_url"]
    print(url)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(f"### Published SDK release\n\n[{tag}]({url})\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_directory", type=Path)
    arguments = parser.parse_args()
    try:
        publish(arguments.artifact_directory)
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print(f"Release publishing failed: {error}", file=sys.stderr)
        sys.exit(1)
