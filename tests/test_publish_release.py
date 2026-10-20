"""Offline contract tests for the CI-only GitHub release publisher."""

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / ".github/scripts/publish_release.py"
SHA = "1" * 40
TAG = "v0.1.0-build.42"
REPOSITORY = "example/xlog_codec"
URL = f"https://github.com/{REPOSITORY}/releases/tag/{TAG}"
NAMES = [f"xlog-codec-0.1.0-{platform}.zip" for platform in
         ("macos-arm64", "windows-x64-mingw", "linux-x64")]


class FakeGitHub:
    """Model the remote state while replacing only the gh process boundary."""

    def __init__(self, files):
        self.files = files
        self.tag_sha = None
        self.release = None
        self.contents = {}
        self.writes = []
        self.failures = {}
        self.downloaded = []
        self.events = []

    def existing_release(self, *, draft=False, names=NAMES, digest=True):
        self.tag_sha = SHA
        self.release = {"id": 100, "tag_name": TAG, "target_commitish": "main", "draft": draft,
                        "prerelease": False, "html_url": URL, "assets": []}
        for index, name in enumerate(names, 1):
            data = self.files[name].read_bytes()
            self.contents[index] = data
            self.release["assets"].append({"id": index, "name": name,
                "state": "uploaded", "size": len(data),
                "digest": "sha256:" + hashlib.sha256(data).hexdigest() if digest else None})

    def __call__(self, command, **kwargs):
        self.events.append(command)
        assert command[0] == "gh"
        assert "test-secret" not in " ".join(command)
        assert kwargs["env"]["GH_TOKEN"] == "test-secret"
        args = command[1:]
        key = tuple(args[:2])
        if key in self.failures:
            code, message = self.failures[key]
            return subprocess.CompletedProcess(command, code, "", message)
        if args[0] == "api":
            endpoint = args[1]
            if endpoint.endswith(f"/commits/tags/{TAG}"):
                assert args[-2:] == ["--jq", ".sha"]
                if self.tag_sha is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: No commit found (HTTP 422)\n")
                value = self.tag_sha
            elif endpoint.endswith(f"/git/ref/tags/{TAG}"):
                if self.tag_sha is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: Not Found (HTTP 404)\n")
                value = json.dumps({"ref": f"refs/tags/{TAG}", "object": {"sha": self.tag_sha}})
            elif endpoint.endswith("/git/refs"):
                assert args[args.index("--method") + 1] == "POST"
                assert f"ref=refs/tags/{TAG}" in args
                assert f"sha={SHA}" in args
                assert self.tag_sha is None
                self.tag_sha = SHA
                self.writes.append("tag")
                value = json.dumps({"ref": f"refs/tags/{TAG}"})
            elif "/releases/assets/" in endpoint:
                asset_id = int(endpoint.rsplit("/", 1)[1])
                assert "Accept: application/octet-stream" in args
                kwargs["stdout"].write(self.contents[asset_id])
                self.downloaded.append(asset_id)
                return subprocess.CompletedProcess(command, 0, None, "")
            elif endpoint.endswith("/releases?per_page=100"):
                assert args[-2:] == ["--paginate", "--slurp"]
                # Put existing releases on a later page, including authenticated drafts.
                value = json.dumps([[], [self.release]] if self.release else [[]])
            elif endpoint.endswith("/releases/100"):
                assert self.release is not None
                value = json.dumps(self.release)
            elif endpoint.endswith(f"/releases/tags/{TAG}"):
                # GitHub's by-tag REST endpoint cannot retrieve drafts.
                if self.release is None or self.release["draft"]:
                    return subprocess.CompletedProcess(command, 1, "", "gh: Not Found (HTTP 404)\n")
                value = json.dumps(self.release)
            else:
                raise AssertionError(f"Unexpected API command: {command}")
        elif args[:2] == ["release", "create"]:
            assert self.tag_sha == SHA
            assert self.release is None
            assert "--draft" in args and "--verify-tag" in args
            assert args[args.index("--target") + 1] == SHA
            assert args[args.index("--repo") + 1] == REPOSITORY
            self.existing_release(draft=True, names=[])
            self.writes.append("create-draft")
            value = URL
        elif args[:2] == ["release", "upload"]:
            assert self.release["draft"]
            assert "--clobber" not in args
            for argument in args[3:args.index("--repo")]:
                name = Path(argument).name
                assert name not in {item["name"] for item in self.release["assets"]}
                data = Path(argument).read_bytes()
                asset_id = len(self.contents) + 1
                self.contents[asset_id] = data
                self.release["assets"].append({"id": asset_id, "name": name,
                    "state": "uploaded", "size": len(data),
                    "digest": "sha256:" + hashlib.sha256(data).hexdigest()})
                self.writes.append(f"upload:{name}")
            value = ""
        elif args[:2] == ["release", "edit"]:
            assert {asset["name"] for asset in self.release["assets"]} == set(NAMES)
            assert "--draft=false" in args and "--latest" in args
            assert "--prerelease=false" in args
            self.release["draft"] = False
            self.release["prerelease"] = False
            self.writes.append("publish")
            value = ""
        else:
            raise AssertionError(f"Unexpected command: {command}")
        return subprocess.CompletedProcess(command, 0, value, "")


class PublishReleaseTest(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location("publish_release", SCRIPT)
        self.publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.publisher)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name) / "artifacts"
        self.directory.mkdir()
        self.files = {}
        for name in NAMES:
            self.files[name] = self.directory / name
            self.files[name].write_bytes(name.encode())
        self.fake = FakeGitHub(self.files)
        environment = {"GITHUB_REPOSITORY": REPOSITORY, "GITHUB_SHA": SHA,
                       "GITHUB_RUN_NUMBER": "42", "GITHUB_RUN_ID": "1234",
                       "GH_TOKEN": "test-secret",
                       "GITHUB_STEP_SUMMARY": str(Path(self.temporary.name) / "summary")}
        self.environment = patch.dict(os.environ, environment, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        process = patch.object(self.publisher.subprocess, "run", self.fake)
        process.start()
        self.addCleanup(process.stop)

    def publish(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.publisher.publish(self.directory)
        return output.getvalue()

    def test_first_run_creates_draft_and_verifies_assets_before_publish(self):
        self.assertIn(URL, self.publish())
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.writes[:2], ["tag", "create-draft"])
        self.assertEqual(self.fake.writes[-1], "publish")
        self.assertEqual(len(self.fake.release["assets"]), 3)
        summary = Path(os.environ["GITHUB_STEP_SUMMARY"]).read_text()
        self.assertIn(URL, summary)

    def test_rejects_missing_asset_before_network(self):
        self.files[NAMES[0]].unlink()
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_rejects_mixed_versions_and_invalid_names_before_network(self):
        for name in ("xlog-codec-0.2.0-macos-arm64.zip", "unexpected.txt"):
            with self.subTest(name=name):
                path = self.directory / name
                self.files[NAMES[0]].rename(path)
                with self.assertRaises(RuntimeError):
                    self.publish()
                path.rename(self.files[NAMES[0]])
        self.assertEqual(self.fake.events, [])

    def test_rejects_extra_file_before_network(self):
        (self.directory / "unexpected.txt").write_text("extra")
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_invalid_run_identity_fails_before_network(self):
        os.environ["GITHUB_RUN_NUMBER"] = "42/other-tag"
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_different_tag_sha_fails_without_writes(self):
        self.fake.tag_sha = "2" * 40
        with self.assertRaisesRegex(RuntimeError, "SHA"):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_existing_tag_without_release_is_reused(self):
        self.fake.tag_sha = SHA
        self.publish()
        self.assertNotIn("tag", self.fake.writes)
        self.assertEqual(self.fake.writes[0], "create-draft")

    def test_published_release_is_idempotent_without_writes(self):
        self.fake.existing_release()
        before = copy.deepcopy(self.fake.release)
        self.assertIn(URL, self.publish())
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.writes, [])

    def test_published_release_without_digests_downloads_and_hashes(self):
        self.fake.existing_release(digest=False)
        self.publish()
        self.assertEqual(self.fake.downloaded, [1, 2, 3])
        self.assertEqual(self.fake.writes, [])

    def test_downloaded_asset_mismatch_is_never_modified(self):
        self.fake.existing_release(digest=False)
        self.fake.contents[1] = b"X" * len(self.fake.contents[1])
        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_unexpected_remote_asset_fails_without_writes(self):
        self.fake.existing_release(draft=True)
        self.fake.release["assets"].append({"name": "unexpected.zip"})
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_published_release_mismatch_or_missing_asset_is_never_modified(self):
        self.fake.existing_release()
        self.fake.release["assets"][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaises(RuntimeError):
            self.publish()
        self.fake.existing_release(names=NAMES[:2])
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_draft_resumes_only_missing_assets(self):
        self.fake.existing_release(draft=True, names=NAMES[:1])
        self.publish()
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(set(self.fake.writes),
                         {f"upload:{NAMES[1]}", f"upload:{NAMES[2]}", "publish"})

    def test_draft_conflicting_asset_is_not_overwritten(self):
        self.fake.existing_release(draft=True, names=NAMES[:1])
        self.fake.release["assets"][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_does_not_swallow_authentication_or_network_errors(self):
        for endpoint in (f"repos/{REPOSITORY}/git/ref/tags/{TAG}",
                         f"repos/{REPOSITORY}/commits/tags/{TAG}",
                         f"repos/{REPOSITORY}/releases?per_page=100"):
            for message in ("gh: Bad credentials (HTTP 401)",
                            "gh: Validation failed (HTTP 422)", "dial tcp: timeout",
                            "proxy returned 404 without HTTP status"):
                with self.subTest(endpoint=endpoint, message=message):
                    self.fake.tag_sha = SHA
                    self.fake.failures = {("api", endpoint): (1, message)}
                    with self.assertRaises(RuntimeError):
                        self.publish()
                    self.assertEqual(self.fake.writes, [])


if __name__ == "__main__":
    unittest.main()
