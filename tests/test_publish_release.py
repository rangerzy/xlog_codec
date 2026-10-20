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
OLD_SHA = "2" * 40
TAG = "v0.1.0"
REPOSITORY = "example/xlog_codec"
URL = f"https://github.com/{REPOSITORY}/releases/tag/{TAG}"
NAMES = [f"xlog-codec-0.1.0-{platform}.zip" for platform in
         ("macos-arm64", "windows-x64-mingw", "linux-x64")]
COMMIT = {"sha": SHA, "html_url": f"https://github.com/{REPOSITORY}/commit/{SHA}",
          "commit": {"message": "Update SDK libraries\n\nDetailed explanation", "author": {
              "name": "SDK Developer", "date": "2026-10-20T01:02:03Z", "email": "private@example.com"}}}
NOTES = (f"SDK version: 0.1.0\nCommit: [{SHA}]({COMMIT['html_url']})\n"
         "Subject: Update SDK libraries\nAuthor: SDK Developer\nDate: 2026-10-20T01:02:03Z\n\n"
         f"Workflow run: https://github.com/{REPOSITORY}/actions/runs/1234\n")


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
        self.stale_release_lookup = False
        self.graphql_response = None
        self.compare_status = "ahead"
        self.compare_commits = [COMMIT]
        self.compare_pages = None
        self.comparisons = {}
        self.change_tag_after_upload = False
        self.corrupt_upload = False
        self.commit_response = COMMIT
        self.ref_response = None
        self.release_response = None
        self.keep_published = False
        self.fail_after_draft = False

    def add_asset(self, name, data, *, digest=True):
        asset_id = max(self.contents, default=0) + 1
        self.contents[asset_id] = data
        self.release["assets"].append({"id": asset_id, "name": name,
            "state": "uploaded", "size": len(data),
            "digest": "sha256:" + hashlib.sha256(data).hexdigest() if digest else None})

    def existing_release(self, *, draft=False, names=NAMES, digest=True, sha=SHA):
        self.tag_sha = sha
        self.release = {"id": 100, "tag_name": TAG, "target_commitish": "main", "draft": draft,
                        "prerelease": False, "immutable": False, "html_url": URL,
                        "name": "xlog-codec 0.1.0", "body": NOTES, "assets": []}
        for name in names:
            self.add_asset(name, self.files[name].read_bytes(), digest=digest)

    def __call__(self, command, **kwargs):
        self.events.append(command)
        assert command[0] == "gh"
        assert "test-secret" not in " ".join(command)
        assert kwargs["env"]["GH_TOKEN"] == "test-secret"
        args = command[1:]
        current_sha = os.environ["GITHUB_SHA"].lower()
        key = tuple(args[:2])
        if key in self.failures:
            code, message = self.failures[key]
            return subprocess.CompletedProcess(command, code, "", message)
        if args[0] == "api":
            endpoint = args[1]
            method = args[args.index("--method") + 1] if "--method" in args else "GET"
            fields = {args[index + 1].split("=", 1)[0]: args[index + 1].split("=", 1)[1]
                      for index, value in enumerate(args) if value in ("-f", "-F")}
            if endpoint == "graphql":
                assert fields["owner"] == "example" and fields["name"] == "xlog_codec" and fields["tag"] == TAG
                found = {"databaseId": 100} if self.release else None
                if self.stale_release_lookup and "create-draft" in self.writes:
                    found = None
                value = self.graphql_response if self.graphql_response is not None else {
                    "data": {"repository": {"release": found}}}
            elif endpoint.endswith(f"/commits/tags/{TAG}"):
                assert args[-2:] == ["--jq", ".sha"]
                if self.tag_sha is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: No commit found (HTTP 422)\n")
                return subprocess.CompletedProcess(command, 0, self.tag_sha, "")
            elif endpoint.endswith(f"/commits/{current_sha}"):
                value = self.commit_response
            elif endpoint.endswith(f"/git/ref/tags/{TAG}"):
                if self.tag_sha is None:
                    return subprocess.CompletedProcess(command, 1, "", "gh: Not Found (HTTP 404)\n")
                value = self.ref_response if self.ref_response is not None else {
                    "ref": f"refs/tags/{TAG}", "object": {"sha": self.tag_sha}}
            elif endpoint.endswith(f"/git/refs/tags/{TAG}"):
                assert method == "PATCH" and fields == {"sha": current_sha, "force": "true"}
                assert self.release is None or self.release["draft"]
                self.tag_sha = current_sha
                self.writes.append("move-tag")
                value = {"ref": f"refs/tags/{TAG}"}
            elif endpoint.endswith("/git/refs"):
                assert method == "POST" and fields == {"ref": f"refs/tags/{TAG}", "sha": SHA}
                assert self.tag_sha is None
                self.tag_sha = SHA
                self.writes.append("tag")
                value = {"ref": f"refs/tags/{TAG}"}
            elif "/compare/" in endpoint:
                assert args[-2:] == ["--paginate", "--slurp"]
                base, head = endpoint.split("/compare/", 1)[1].split("?", 1)[0].split("...")
                assert endpoint.endswith("?per_page=100")
                if (base, head) in self.comparisons:
                    value = [self.comparisons[base, head]]
                else:
                    assert base == OLD_SHA and head == SHA
                    value = self.compare_pages if self.compare_pages is not None else [{
                        "status": self.compare_status, "total_commits": len(self.compare_commits),
                        "html_url": f"https://github.com/{REPOSITORY}/compare/{OLD_SHA}...{SHA}",
                        "commits": self.compare_commits}]
            elif "/releases/assets/" in endpoint:
                asset_id = int(endpoint.rsplit("/", 1)[1])
                assert "Accept: application/octet-stream" in args
                kwargs["stdout"].write(self.contents[asset_id])
                self.downloaded.append(asset_id)
                return subprocess.CompletedProcess(command, 0, None, "")
            elif endpoint.endswith("/releases/100"):
                assert self.release is not None
                if method == "PATCH":
                    assert fields["tag_name"] == TAG
                    assert not self.release["immutable"]
                    if "draft" in fields:
                        assert fields["draft"] == "true"
                        if not self.keep_published:
                            self.release["draft"] = True
                        self.writes.append("make-draft")
                    if "body" in fields and not self.keep_published:
                        assert self.release["draft"]
                        self.release["body"] = fields["body"]
                        self.release["name"] = fields["name"]
                        self.writes.append("notes")
                    if "draft" in fields and self.fail_after_draft:
                        return subprocess.CompletedProcess(command, 1, "", "connection reset after server update")
                value = self.release_response if self.release_response is not None else self.release
            elif endpoint.endswith("/releases"):
                assert method == "POST"
                assert self.tag_sha == SHA and self.release is None
                assert fields["tag_name"] == TAG and fields["target_commitish"] == SHA
                assert fields["draft"] == "true" and fields["prerelease"] == "false"
                assert fields["name"] == "xlog-codec 0.1.0"
                self.existing_release(draft=True, names=[])
                self.release["body"] = fields["body"]
                self.writes.append("create-draft")
                value = self.release
            else:
                raise AssertionError(f"Unexpected API command: {command}")
        elif args[:2] == ["release", "upload"]:
            assert self.release["draft"] and "--clobber" in args
            for argument in args[3:args.index("--repo")]:
                name = Path(argument).name
                self.release["assets"] = [asset for asset in self.release["assets"] if asset["name"] != name]
                data = Path(argument).read_bytes()
                if self.corrupt_upload:
                    data = b"X" * len(data)
                self.add_asset(name, data)
                self.writes.append(f"upload:{name}")
            if self.change_tag_after_upload:
                self.tag_sha = OLD_SHA
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
        return subprocess.CompletedProcess(command, 0, value if isinstance(value, str) else json.dumps(value), "")


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
                       "GITHUB_RUN_ID": "1234", "GH_TOKEN": "test-secret",
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

    def test_first_run_creates_version_release_with_commit_details_before_publish(self):
        self.assertIn(URL, self.publish())
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.writes[:2], ["tag", "create-draft"])
        self.assertEqual(self.fake.writes[-1], "publish")
        self.assertEqual(len(self.fake.release["assets"]), 3)
        self.assertEqual(self.fake.release["name"], "xlog-codec 0.1.0")
        self.assertEqual(self.fake.release["body"], NOTES)
        self.assertNotIn("private@example.com", self.fake.release["body"])
        self.assertIn(URL, Path(os.environ["GITHUB_STEP_SUMMARY"]).read_text())

    def test_run_number_does_not_change_version_tag(self):
        os.environ["GITHUB_RUN_NUMBER"] = "not-used"
        self.publish()
        self.assertEqual(self.fake.release["tag_name"], "v0.1.0")

    def test_first_run_uses_creation_response_when_release_lookup_stays_empty(self):
        self.fake.stale_release_lookup = True
        self.assertIn(URL, self.publish())
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(sum(command[1:3] == ["api", "graphql"] for command in self.fake.events), 1)

    def test_graphql_errors_or_missing_repository_are_not_missing_release(self):
        for response in ({"data": {"repository": None}},
                         {"errors": [{"message": "forbidden"}], "data": {"repository": {"release": None}}},
                         {"data": {"repository": {}}},
                         {"data": {"repository": {"release": {"databaseId": None}}}}):
            with self.subTest(response=response):
                self.fake.graphql_response = response
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_invalid_artifact_set_fails_before_network(self):
        self.files[NAMES[0]].unlink()
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_mixed_versions_invalid_names_and_extra_files_fail_before_network(self):
        for name in ("xlog-codec-0.2.0-macos-arm64.zip", "unexpected.txt"):
            with self.subTest(name=name):
                path = self.directory / name
                self.files[NAMES[0]].rename(path)
                with self.assertRaises(RuntimeError):
                    self.publish()
                path.rename(self.files[NAMES[0]])
        (self.directory / "unexpected.txt").write_text("extra")
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.events, [])

    def test_invalid_run_identity_fails_before_network(self):
        for name, invalid in (("GITHUB_RUN_ID", "42/other-tag"), ("GITHUB_SHA", "invalid"),
                              ("GITHUB_REPOSITORY", "invalid/repo/more")):
            with self.subTest(name=name), patch.dict(os.environ, {name: invalid}):
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.events, [])

    def test_existing_tag_without_release_is_reused(self):
        self.fake.tag_sha = SHA
        self.publish()
        self.assertNotIn("tag", self.fake.writes)
        self.assertEqual(self.fake.writes[0], "create-draft")

    def test_published_matching_release_is_idempotent_without_writes(self):
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

    def test_same_commit_changed_zip_is_replaced_on_same_release(self):
        self.fake.existing_release()
        self.files[NAMES[0]].write_bytes(b"rebuilt SDK")
        self.publish()
        self.assertEqual(self.fake.release["id"], 100)
        self.assertEqual(self.fake.release["html_url"], URL)
        self.assertEqual(self.fake.writes, ["make-draft", "notes", f"upload:{NAMES[0]}", "publish"])
        asset = next(asset for asset in self.fake.release["assets"] if asset["name"] == NAMES[0])
        self.assertEqual(self.fake.contents[asset["id"]], b"rebuilt SDK")

    def test_downloaded_mismatched_asset_is_replaced_and_verified(self):
        self.fake.existing_release(digest=False)
        self.fake.contents[1] = b"X" * len(self.fake.contents[1])
        self.publish()
        self.assertIn(f"upload:{NAMES[0]}", self.fake.writes)
        self.assertEqual(self.fake.writes[-1], "publish")

    def test_new_commit_updates_tag_notes_and_assets_on_same_release(self):
        self.fake.existing_release(sha=OLD_SHA)
        self.files[NAMES[0]].write_bytes(b"new SDK")
        self.publish()
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertEqual(self.fake.release["id"], 100)
        self.assertEqual(self.fake.writes[:3], ["make-draft", "notes", "move-tag"])
        self.assertEqual(self.fake.writes[-1], "publish")
        body = self.fake.release["body"]
        self.assertIn(f"/compare/{OLD_SHA}...{SHA}", body)
        self.assertIn("Update SDK libraries", body)
        self.assertIn("SDK Developer", body)
        self.assertIn("2026-10-20T01:02:03Z", body)
        self.assertNotIn("private@example.com", body)

    def test_compare_notes_include_all_pages_without_arbitrary_truncation(self):
        self.fake.existing_release(sha=OLD_SHA)
        commits = [copy.deepcopy(COMMIT) for _ in range(21)]
        for index, commit in enumerate(commits):
            commit["sha"] = f"{index + 10:040x}"
            commit["html_url"] = f"https://github.com/{REPOSITORY}/commit/{commit['sha']}"
            commit["commit"]["message"] = f"Change number {index + 1}"
        self.fake.compare_pages = [{"status": "ahead", "total_commits": 21,
                                   "commits": commits[:10]},
                                  {"status": "ahead", "total_commits": 21,
                                   "commits": commits[10:]}]
        self.publish()
        for commit in commits:
            self.assertIn(commit["sha"], self.fake.release["body"])
            self.assertIn(commit["commit"]["message"], self.fake.release["body"])

    def test_published_missing_asset_is_restored_before_latest(self):
        self.fake.existing_release(names=NAMES[:2])
        self.publish()
        self.assertEqual(self.fake.writes[0], "make-draft")
        self.assertIn(f"upload:{NAMES[2]}", self.fake.writes)
        self.assertEqual(self.fake.writes[-1], "publish")

    def test_partial_draft_resumes_and_replaces_conflicting_asset(self):
        self.fake.existing_release(draft=True, names=NAMES[:1])
        self.fake.release["assets"][0]["digest"] = "sha256:" + "0" * 64
        self.publish()
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(set(self.fake.writes), {"notes", *(f"upload:{name}" for name in NAMES), "publish"})

    def test_draft_rerun_preserves_notes_for_already_moved_tag(self):
        self.fake.existing_release(draft=True, names=NAMES[:1])
        self.fake.release["body"] = NOTES + f"\nChanges: https://github.com/{REPOSITORY}/compare/{OLD_SHA}...{SHA}\n"
        before = self.fake.release["body"]
        self.publish()
        self.assertEqual(self.fake.release["body"], before)

    def test_failed_update_then_new_commit_preserves_all_unpublished_changes(self):
        # A is published; B persists its notes and tag, then its upload fails.
        self.fake.existing_release(sha=OLD_SHA)
        self.files[NAMES[0]].write_bytes(b"B SDK")
        self.fake.failures = {("release", "upload"): (1, "upload interrupted")}
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertTrue(self.fake.release["draft"])
        failed_notes = self.fake.release["body"]
        # C takes over the draft and must include A...B as well as B...C.
        next_sha = "3" * 40
        next_commit = copy.deepcopy(COMMIT)
        next_commit["sha"] = next_sha
        next_commit["commit"]["message"] = "Complete SDK update"
        self.fake.commit_response = next_commit
        self.fake.comparisons = {
            (SHA, next_sha): {"status": "ahead", "total_commits": 1, "commits": [next_commit]},
            (OLD_SHA, next_sha): {"status": "ahead", "total_commits": 2, "commits": [COMMIT, next_commit]}}
        self.fake.failures = {}
        self.files[NAMES[0]].write_bytes(b"C SDK")
        with patch.dict(os.environ, {"GITHUB_SHA": next_sha}):
            self.publish()
        self.assertEqual(self.fake.tag_sha, next_sha)
        self.assertFalse(self.fake.release["draft"])
        self.assertEqual(self.fake.release["id"], 100)
        self.assertIn(f"<!-- xlog-release-base: {OLD_SHA} -->", failed_notes)
        self.assertIn(f"/compare/{OLD_SHA}...{next_sha}", self.fake.release["body"])
        self.assertIn("Update SDK libraries", self.fake.release["body"])
        self.assertIn("Complete SDK update", self.fake.release["body"])

    def test_failed_same_commit_rebuild_uses_last_published_commit_as_new_base(self):
        self.fake.existing_release()
        self.fake.release["body"] += f"\nChanges: https://github.com/{REPOSITORY}/compare/{OLD_SHA}...{SHA}\n"
        self.fake.release["body"] += f"\n<!-- xlog-release-base: {OLD_SHA} -->\n"
        self.files[NAMES[0]].write_bytes(b"rebuilt B SDK")
        self.fake.failures = {("release", "upload"): (1, "upload interrupted")}
        with self.assertRaises(RuntimeError):
            self.publish()
        failed_notes = self.fake.release["body"]
        self.assertTrue(self.fake.release["draft"])
        self.assertEqual(self.fake.tag_sha, SHA)
        next_sha = "3" * 40
        next_commit = copy.deepcopy(COMMIT)
        next_commit["sha"] = next_sha
        next_commit["commit"]["message"] = "C SDK update"
        self.fake.commit_response = next_commit
        self.fake.comparisons = {
            (SHA, next_sha): {"status": "ahead", "total_commits": 1, "commits": [next_commit]},
            (OLD_SHA, next_sha): {"status": "ahead", "total_commits": 2, "commits": [COMMIT, next_commit]}}
        self.fake.failures = {}
        self.files[NAMES[0]].write_bytes(b"C SDK")
        with patch.dict(os.environ, {"GITHUB_SHA": next_sha}):
            self.publish()
        self.assertIn(f"<!-- xlog-release-base: {SHA} -->", failed_notes)
        self.assertNotIn(f"<!-- xlog-release-base: {OLD_SHA} -->", failed_notes)
        self.assertIn(f"/compare/{SHA}...{next_sha}", self.fake.release["body"])
        self.assertNotIn(f"/compare/{OLD_SHA}...{next_sha}", self.fake.release["body"])
        self.assertFalse(self.fake.release["draft"])

    def test_interrupted_draft_conversion_atomically_persists_current_published_base(self):
        self.fake.existing_release()
        self.fake.release["body"] += f"\n<!-- xlog-release-base: {OLD_SHA} -->\n"
        self.files[NAMES[0]].write_bytes(b"rebuilt B SDK")
        self.fake.fail_after_draft = True
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertTrue(self.fake.release["draft"])
        self.assertEqual(self.fake.tag_sha, SHA)
        self.assertIn(f"<!-- xlog-release-base: {SHA} -->", self.fake.release["body"])
        self.assertNotIn(f"<!-- xlog-release-base: {OLD_SHA} -->", self.fake.release["body"])
        self.assertNotIn("publish", self.fake.writes)
        self.assertFalse(any(write.startswith("upload:") for write in self.fake.writes))
        request = next(command for command in self.fake.events if "draft=true" in command)
        self.assertIn(f"tag_name={TAG}", request)
        self.assertIn("name=xlog-codec 0.1.0", request)
        self.assertTrue(any(argument.startswith("body=") for argument in request))

    def test_published_release_uses_actual_tag_instead_of_old_base_marker(self):
        self.fake.existing_release(sha=OLD_SHA)
        obsolete_base = "4" * 40
        self.fake.release["body"] += f"\n<!-- xlog-release-base: {obsolete_base} -->\n"
        self.publish()
        self.assertIn(f"/compare/{OLD_SHA}...{SHA}", self.fake.release["body"])
        self.assertNotIn(obsolete_base, self.fake.release["body"])

    def test_draft_base_marker_does_not_override_actual_tag_rollback_guard(self):
        self.fake.existing_release(draft=True, sha=OLD_SHA)
        self.fake.release["body"] += f"\n<!-- xlog-release-base: {'4' * 40} -->\n"
        self.fake.compare_status = "behind"
        before = copy.deepcopy(self.fake.release)
        self.assertIn("Skipped", self.publish())
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.writes, [])

    def test_oversized_utf8_current_subject_fails_before_mutation(self):
        self.fake.existing_release(sha=OLD_SHA)
        self.fake.commit_response = copy.deepcopy(COMMIT)
        self.fake.commit_response["commit"]["message"] = "界" * 21000
        before = copy.deepcopy(self.fake.release)
        with self.assertRaisesRegex(RuntimeError, "60000"):
            self.publish()
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.writes, [])
        self.assertEqual(self.fake.tag_sha, OLD_SHA)

    def test_oversized_complete_commit_list_fails_before_mutation(self):
        self.fake.existing_release(sha=OLD_SHA)
        self.fake.compare_commits = []
        for index in range(250):
            commit = copy.deepcopy(COMMIT)
            commit["sha"] = f"{index + 10:040x}"
            commit["commit"]["message"] = f"Change {index}: " + "界" * 40
            self.fake.compare_commits.append(commit)
        before = copy.deepcopy(self.fake.release)
        with self.assertRaisesRegex(RuntimeError, "60000"):
            self.publish()
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.writes, [])
        self.assertEqual(self.fake.tag_sha, OLD_SHA)

    def test_older_run_skips_without_changing_release_tag_assets_or_latest(self):
        self.fake.existing_release(sha=OLD_SHA)
        self.fake.compare_status = "behind"
        before = copy.deepcopy(self.fake.release)
        self.assertIn("Skipped", self.publish())
        self.assertEqual(self.fake.release, before)
        self.assertEqual(self.fake.tag_sha, OLD_SHA)
        self.assertEqual(self.fake.writes, [])
        self.assertNotIn("Published SDK release", Path(os.environ["GITHUB_STEP_SUMMARY"]).read_text())

    def test_diverged_or_unknown_comparison_fails_without_mutation(self):
        self.fake.existing_release(sha=OLD_SHA)
        for status in ("diverged", "unknown"):
            with self.subTest(status=status):
                self.fake.compare_status = status
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])

    def test_immutable_release_fails_without_any_mutation(self):
        self.fake.existing_release(sha=OLD_SHA)
        self.fake.release["immutable"] = True
        with self.assertRaisesRegex(RuntimeError, "[Ii]mmutable"):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_unexpected_or_duplicate_remote_assets_fail_without_writes(self):
        self.fake.existing_release(draft=True)
        for name in ("unexpected.zip", NAMES[0]):
            with self.subTest(name=name):
                self.fake.release["assets"].append({"name": name})
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.fake.release["assets"].pop()
                self.assertEqual(self.fake.writes, [])

    def test_tag_change_during_upload_prevents_publish(self):
        self.fake.change_tag_after_upload = True
        with self.assertRaisesRegex(RuntimeError, "SHA"):
            self.publish()
        self.assertNotIn("publish", self.fake.writes)
        self.assertTrue(self.fake.release["draft"])

    def test_uploaded_hash_mismatch_prevents_publish(self):
        self.fake.corrupt_upload = True
        with self.assertRaisesRegex(RuntimeError, "SHA-256"):
            self.publish()
        self.assertNotIn("publish", self.fake.writes)
        self.assertTrue(self.fake.release["draft"])

    def test_commit_response_identity_is_checked_before_mutation(self):
        self.fake.commit_response = {**COMMIT, "sha": OLD_SHA}
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_invalid_ref_or_release_identity_fails_before_mutation(self):
        self.fake.existing_release()
        for ref in ({}, {"ref": "refs/tags/other", "object": {"sha": SHA}}):
            with self.subTest(ref=ref):
                self.fake.ref_response = ref
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])
        self.fake.ref_response = None
        self.fake.release_response = {**self.fake.release, "id": 999}
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_malformed_current_or_comparison_commit_fails_before_mutation(self):
        self.fake.existing_release(sha=OLD_SHA)
        for bad in ({**COMMIT, "commit": None}, {**COMMIT, "sha": "invalid"},
                    {**COMMIT, "commit": {"message": "Subject", "author": {"name": "Author"}}}):
            with self.subTest(commit=bad):
                self.fake.commit_response = bad
                with self.assertRaises(RuntimeError):
                    self.publish()
                self.assertEqual(self.fake.writes, [])
        self.fake.commit_response = COMMIT
        self.fake.compare_commits = [{**COMMIT, "commit": None}]
        with self.assertRaises(RuntimeError):
            self.publish()
        self.assertEqual(self.fake.writes, [])

    def test_failed_draft_conversion_does_not_move_tag_or_replace_assets(self):
        self.fake.existing_release(sha=OLD_SHA)
        self.fake.keep_published = True
        with self.assertRaisesRegex(RuntimeError, "draft"):
            self.publish()
        self.assertEqual(self.fake.writes, ["make-draft"])
        self.assertEqual(self.fake.tag_sha, OLD_SHA)

    def test_does_not_swallow_authentication_or_network_errors(self):
        for endpoint in (f"repos/{REPOSITORY}/git/ref/tags/{TAG}",
                         f"repos/{REPOSITORY}/commits/tags/{TAG}",
                         f"repos/{REPOSITORY}/commits/{SHA}", "graphql",
                         f"repos/{REPOSITORY}/compare/{OLD_SHA}...{SHA}?per_page=100"):
            for message in ("gh: Bad credentials (HTTP 401)", "gh: Validation failed (HTTP 422)",
                            "dial tcp: timeout", "proxy returned 404 without HTTP status"):
                with self.subTest(endpoint=endpoint, message=message):
                    self.fake.tag_sha = OLD_SHA
                    self.fake.failures = {("api", endpoint): (1, message)}
                    with self.assertRaises(RuntimeError):
                        self.publish()
                    self.assertEqual(self.fake.writes, [])


if __name__ == "__main__":
    unittest.main()
