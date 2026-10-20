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
    release = api(f"repos/{repository}/releases/{release_id}")
    if not isinstance(release, dict) or release.get("id") != release_id:
        raise RuntimeError("GitHub returned an unexpected release ID")
    return release


def verify_assets(release, files, repository, *, complete, replace=False):
    assets = release["assets"]
    names = [asset["name"] for asset in assets]
    if len(names) != len(set(names)) or set(names) - files.keys():
        raise RuntimeError("Release contains duplicate or unexpected assets")
    if complete and set(names) != files.keys():
        raise RuntimeError("Release does not contain the complete three ZIPs")
    verified = set()
    for asset in assets:
        path = files[asset["name"]]
        expected = hashlib.sha256(path.read_bytes()).hexdigest()
        if asset["state"] != "uploaded" or asset["size"] != path.stat().st_size:
            if replace:
                continue
            raise RuntimeError(f"Incomplete or different asset: {path.name}")
        digest = asset.get("digest")
        if not digest:
            with tempfile.TemporaryFile() as downloaded:
                gh("api", f"repos/{repository}/releases/assets/{asset['id']}",
                   "-H", "Accept: application/octet-stream", output=downloaded)
                downloaded.seek(0)
                digest = "sha256:" + hashlib.sha256(downloaded.read()).hexdigest()
        if digest != "sha256:" + expected:
            if replace:
                continue
            raise RuntimeError(f"Asset SHA-256 differs: {path.name}")
        verified.add(path.name)
    return verified


def commit_details(commit, repository, *, sha=None):
    if not isinstance(commit, dict):
        raise RuntimeError("GitHub returned invalid commit metadata")
    commit_sha = commit.get("sha", "")
    if (not isinstance(commit_sha, str) or not re.fullmatch(r"[0-9a-fA-F]{40}", commit_sha)
            or (sha is not None and commit_sha.lower() != sha.lower())):
        raise RuntimeError("GitHub returned an unexpected commit SHA")
    details = commit.get("commit")
    author = details.get("author") if isinstance(details, dict) else None
    if (not isinstance(author, dict) or not isinstance(details.get("message"), str)
            or any(not isinstance(author.get(key), str) or not author[key] for key in ("name", "date"))):
        raise RuntimeError("GitHub returned incomplete commit metadata")
    # Read only the subject and public attribution, never email or raw API output.
    lines = details["message"].splitlines()
    return (f"[{commit_sha}](https://github.com/{repository}/commit/{commit_sha})",
            lines[0] if lines else "(no subject)", author["name"], author["date"])


def compare_commits(repository, old_sha, sha):
    pages = api(f"repos/{repository}/compare/{old_sha}...{sha}?per_page=100",
                "--paginate", "--slurp")
    if not isinstance(pages, list) or not pages or any(not isinstance(page, dict) for page in pages):
        raise RuntimeError("GitHub returned an invalid commit comparison")
    status = pages[0].get("status")
    if status not in {"ahead", "identical", "behind", "diverged"}:
        raise RuntimeError("GitHub returned an invalid comparison status")
    if status == "diverged":
        raise RuntimeError("Existing release tag and workflow SHA have diverged")
    if status == "behind":
        return status, []
    commits = [commit for page in pages for commit in page["commits"]]
    if (any(page.get("status") != status for page in pages)
            or type(pages[0].get("total_commits")) is not int
            or len(commits) != pages[0]["total_commits"]):
        raise RuntimeError("GitHub returned an incomplete commit comparison")
    return status, commits


def report(tag, release, *, skipped=False):
    url = release["html_url"] if release else f"https://github.com/{os.environ['GITHUB_REPOSITORY']}/releases/tag/{tag}"
    heading = "Skipped outdated SDK release update" if skipped else "Published SDK release"
    print(f"Skipped outdated workflow: {url}" if skipped else url)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(f"### {heading}\n\n[{tag}]({url})\n")


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
        "GITHUB_RUN_ID": r"[1-9][0-9]*",
    }.items():
        values[key] = os.environ.get(key, "")
        if not re.fullmatch(pattern, values[key]):
            raise RuntimeError(f"Missing or invalid {key}")
    repository, sha = values["GITHUB_REPOSITORY"], values["GITHUB_SHA"].lower()
    tag = f"v{version}"
    commit_endpoint = f"repos/{repository}/commits/tags/{tag}"
    release = find_release(repository, tag)
    if release and release["tag_name"] != tag:
        raise RuntimeError("GitHub returned an unexpected release tag")
    # Only the exact ref endpoint's 404 means absent; commit lookups may return 422.
    ref = api(f"repos/{repository}/git/ref/tags/{tag}", missing=True)
    if ref is not None and (not isinstance(ref, dict) or ref.get("ref") != f"refs/tags/{tag}"):
        raise RuntimeError("GitHub returned an unexpected tag ref")
    old_sha = gh("api", commit_endpoint, "--jq", ".sha").lower() if ref is not None else None
    if old_sha is not None and not re.fullmatch(r"[0-9a-f]{40}", old_sha):
        raise RuntimeError("GitHub returned an invalid tag SHA")
    changes = []
    if old_sha is not None and old_sha != sha:
        status, changes = compare_commits(repository, old_sha, sha)
        if status == "behind":
            report(tag, release, skipped=True)
            return
    if release and release.get("immutable"):
        raise RuntimeError("Immutable release cannot be updated")
    notes_base = old_sha
    if release and release["draft"]:
        marker = re.search(r"(?m)^<!-- xlog-release-base: ([0-9a-f]{40}) -->$", release.get("body") or "")
        if marker:
            notes_base = marker[1]
        if notes_base != old_sha:
            # The tag may have moved during a failed upload; retain all unpublished changes.
            status, changes = compare_commits(repository, notes_base, sha)
            if status == "behind":
                raise RuntimeError("Draft release base is ahead of the workflow SHA")
    commit = api(f"repos/{repository}/commits/{sha}")
    link, subject, author, date = commit_details(commit, repository, sha=sha)
    prefix = (f"SDK version: {version}\nCommit: {link}\n"
              f"Subject: {subject}\nAuthor: {author}\nDate: {date}\n")
    notes = prefix + f"\nWorkflow run: https://github.com/{repository}/actions/runs/{values['GITHUB_RUN_ID']}\n"
    if notes_base is not None and notes_base != sha:
        notes += f"\nChanges: https://github.com/{repository}/compare/{notes_base}...{sha}\n\n"
        for change in changes:
            link, subject, author, date = commit_details(change, repository)
            notes += f"- {link} {subject} — {author}, {date}\n"
        notes += f"\n<!-- xlog-release-base: {notes_base} -->\n"
    elif release and (release.get("body") or "").startswith(prefix):
        # A failed upload may have already moved the tag; retain its comparison notes.
        notes = release["body"]
    if len(notes.encode("utf-8")) > 60000:
        raise RuntimeError("Release notes exceed 60000 UTF-8 bytes")
    title = f"xlog-codec {version}"
    present = verify_assets(release, files, repository, complete=False, replace=True) if release else set()
    if (release and not release["draft"] and not release["prerelease"]
            and old_sha == sha and present == files.keys()
            and release.get("name") == title and release.get("body") == notes):
        report(tag, release)
        return
    if release and not release["draft"]:
        # A new draft starts at the actual published tag, even after a same-SHA rebuild.
        notes = re.sub(r"(?m)^<!-- xlog-release-base: [0-9a-f]{40} -->\n?", "", notes).rstrip() + "\n"
        if old_sha is not None:
            notes += f"\n<!-- xlog-release-base: {old_sha} -->\n"
        if len(notes.encode("utf-8")) > 60000:
            raise RuntimeError("Release notes exceed 60000 UTF-8 bytes")
    if old_sha is not None and gh("api", commit_endpoint, "--jq", ".sha").lower() != old_sha:
        raise RuntimeError("Tag SHA changed before updating the release")
    if release:
        release_endpoint = f"repos/{repository}/releases/{release['id']}"
        fields = ["-f", f"tag_name={tag}", "-f", f"name={title}", "-f", f"body={notes}"]
        if not release["draft"]:
            fields += ["-F", "draft=true"]
        # Persist the baseline and draft state atomically before moving the tag.
        release = api(release_endpoint, "--method", "PATCH", *fields)
        if not release["draft"]:
            raise RuntimeError("Release did not become a draft")
    if old_sha is None:
        gh("api", f"repos/{repository}/git/refs", "--method", "POST",
           "-f", f"ref=refs/tags/{tag}", "-f", f"sha={sha}")
    elif old_sha != sha:
        gh("api", f"repos/{repository}/git/refs/tags/{tag}", "--method", "PATCH",
           "-f", f"sha={sha}", "-F", "force=true")
    if release is None:
        # Consume the creation response: GraphQL/list reads may lag a successful write.
        release = api(f"repos/{repository}/releases", "--method", "POST",
                      "-f", f"tag_name={tag}", "-f", f"target_commitish={sha}",
                      "-f", f"name={title}", "-f", f"body={notes}",
                      "-F", "draft=true", "-F", "prerelease=false")
    release_endpoint = f"repos/{repository}/releases/{release['id']}"
    if release["tag_name"] != tag or not release["draft"]:
        raise RuntimeError("GitHub returned an unexpected draft release")
    missing = [str(path) for name, path in files.items() if name not in present]
    if missing:
        gh("release", "upload", tag, *missing, "--repo", repository, "--clobber")
    release = api(release_endpoint)
    verify_assets(release, files, repository, complete=True)
    # Never rely on target_commitish: the actual tag must match the validated ZIPs.
    if gh("api", commit_endpoint, "--jq", ".sha").lower() != sha:
        raise RuntimeError("Tag SHA changed before publishing")
    gh("release", "edit", tag, "--repo", repository,
       "--draft=false", "--prerelease=false", "--latest")
    release = api(release_endpoint)
    if release["draft"] or release["prerelease"]:
        raise RuntimeError("Release did not become a published ordinary release")
    report(tag, release)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_directory", type=Path)
    arguments = parser.parse_args()
    try:
        publish(arguments.artifact_directory)
    except (RuntimeError, OSError, ValueError, KeyError) as error:
        print(f"Release publishing failed: {error}", file=sys.stderr)
        sys.exit(1)
