import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import publish_release as publisher


class PublishReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "tooth.json").write_text(json.dumps({"version": "26.60.0"}), encoding="utf-8")
        self.commit = "a" * 40
        self.data = b"original Actions ZIP"
        self.download = self.data
        self.digest = "sha256:" + hashlib.sha256(self.data).hexdigest()
        self.metadata = {"expired": False, "name": f"LHolo-{self.commit}-client-windows-x64",
                         "workflow_run": {"id": 123}, "digest": self.digest}
        self.notes = "- Adapt Minecraft Bedrock `1.26.60.05`\n"
        self.after_notes = self.notes
        self.after_digest = self.digest
        self.uploads = []
        self.views = 0
        for target, kwargs in (
            ("release_package.PROJECT", {"new": self.root}),
            ("tag_commit", {"return_value": self.commit}),
            ("subprocess.check_output", {"return_value": self.commit + "\n"}),
            ("gh_json", {"side_effect": self.gh_json}),
            ("subprocess.run", {"side_effect": self.run_command}),
            ("release_package.verify_archive", {}),
        ):
            mock = patch.object(publisher.release_package, "PROJECT", self.root) if target == "release_package.PROJECT" else patch(
                "publish_release." + target, **kwargs)
            value = mock.start()
            self.addCleanup(mock.stop)
            if target == "release_package.verify_archive":
                self.verify = value
            if target == "tag_commit":
                self.resolve_tag = value

    def gh_json(self, *args):
        if args[0] == "api":
            return copy.deepcopy(self.metadata)
        self.views += 1
        return {"body": self.notes if self.views == 1 else self.after_notes,
                "assets": [] if self.views == 1 else [
                    {"name": "LHolo-client-windows-x64.zip", "digest": self.after_digest}]}

    def run_command(self, args, **kwargs):
        if args[:2] == ["gh", "api"]:
            kwargs["stdout"].write(self.download)
        else:
            self.assertEqual(args[:3], ["gh", "release", "upload"])
            self.uploads.append(args)

    def publish(self, tag="v26.60.0"):
        publisher.publish("owner/LHolo", tag, self.commit, 123, 456, self.root / "release")

    def test_original_zip_uploaded_without_repacking_or_editing_notes(self):
        self.publish()
        self.assertEqual((self.root / "release/LHolo-client-windows-x64.zip").read_bytes(), self.data)
        self.assertEqual(len(self.uploads), 1)
        self.verify.assert_called_once_with(self.root / "release/LHolo-client-windows-x64.zip", "26.60.0")

    def test_wrong_tag_version_never_uploads(self):
        with self.assertRaisesRegex(ValueError, "tooth version"):
            self.publish("v26.51.10")
        self.assertFalse(self.uploads)

    def test_different_source_commit_never_uploads(self):
        self.resolve_tag.return_value = "b" * 40
        with self.assertRaisesRegex(ValueError, "build commit differ"):
            self.publish()
        self.assertFalse(self.uploads)

    def test_foreign_expired_or_misnamed_artifact_never_uploads(self):
        original = copy.deepcopy(self.metadata)
        for field, value in (("expired", True), ("name", "other-build"), ("workflow_run", {"id": 999})):
            with self.subTest(field=field):
                self.metadata = {**original, field: value}
                with self.assertRaisesRegex(ValueError, "this release build"):
                    self.publish()
                self.assertFalse(self.uploads)

    def test_missing_digest_never_uploads(self):
        self.metadata["digest"] = ""
        with self.assertRaisesRegex(ValueError, "Missing GitHub artifact SHA256"):
            self.publish()
        self.assertFalse(self.uploads)

    def test_corrupt_download_never_uploads(self):
        self.download = b"tampered"
        with self.assertRaisesRegex(ValueError, "Downloaded artifact SHA256"):
            self.publish()
        self.assertFalse(self.uploads)

    def test_invalid_installation_inventory_never_uploads(self):
        self.verify.side_effect = ValueError("invalid inventory")
        with self.assertRaisesRegex(ValueError, "invalid inventory"):
            self.publish()
        self.assertFalse(self.uploads)

    def test_tag_moved_during_download_never_uploads(self):
        self.resolve_tag.side_effect = [self.commit, "b" * 40]
        with self.assertRaisesRegex(ValueError, "tag moved"):
            self.publish()
        self.assertFalse(self.uploads)

    def test_uploaded_digest_and_notes_are_checked(self):
        self.after_digest = "sha256:" + "0" * 64
        with self.assertRaisesRegex(ValueError, "Uploaded release asset SHA256"):
            self.publish()
        self.after_digest = self.digest
        self.after_notes = "changed notes"
        self.views = 0
        with self.assertRaisesRegex(ValueError, "Release notes changed"):
            self.publish()


if __name__ == "__main__":
    unittest.main()
