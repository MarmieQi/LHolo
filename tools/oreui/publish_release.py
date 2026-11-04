"""Attach this release workflow's verified original Actions ZIP without editing notes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import quote

import release_package


def gh_json(*args):
    return json.loads(subprocess.check_output(["gh", *args], text=True, encoding="utf-8"))


def tag_commit(repo, tag):
    obj = gh_json("api", f"repos/{repo}/git/ref/tags/{quote(tag, safe='')}")["object"]
    while obj["type"] == "tag":
        obj = gh_json("api", f"repos/{repo}/git/tags/{obj['sha']}")["object"]
    if obj["type"] != "commit":
        raise ValueError("Release tag does not point to a commit")
    return obj["sha"]


def publish(repo, tag, commit, run_id, artifact_id, directory):
    version = json.loads((release_package.PROJECT / "tooth.json").read_text(encoding="utf-8"))["version"]
    if tag != "v" + version:
        raise ValueError("Release tag and tooth version differ")
    checked_out = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    if checked_out != commit or tag_commit(repo, tag) != commit:
        raise ValueError("Release tag, checked-out source and build commit differ")
    before = gh_json("release", "view", tag, "--repo", repo, "--json", "body,assets")
    endpoint = f"repos/{repo}/actions/artifacts/{artifact_id}"
    metadata = gh_json("api", endpoint)
    expected_name = f"{repo.rsplit('/', 1)[-1]}-{commit}-client-windows-x64"
    if (metadata["expired"] or metadata["name"] != expected_name
            or metadata["workflow_run"]["id"] != run_id):
        raise ValueError("Artifact does not belong to this release build")
    digest = metadata.get("digest", "")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", digest):
        raise ValueError("Missing GitHub artifact SHA256")
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / "LHolo-client-windows-x64.zip"
    with archive.open("wb") as output:
        subprocess.run(["gh", "api", endpoint + "/zip"], stdout=output, check=True)
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != "sha256:" + actual:
        raise ValueError("Downloaded artifact SHA256 mismatch")
    release_package.checksum_path(archive).write_text(f"{actual}  {archive.name}\n", encoding="ascii")
    release_package.verify_archive(archive, version)
    if tag_commit(repo, tag) != commit:
        raise ValueError("Release tag moved during the build")
    subprocess.run(["gh", "release", "upload", tag, str(archive), "--repo", repo, "--clobber"], check=True)
    after = gh_json("release", "view", tag, "--repo", repo, "--json", "body,assets")
    uploaded = [asset for asset in after["assets"] if asset["name"] == archive.name]
    if len(uploaded) != 1 or uploaded[0].get("digest") != digest:
        raise ValueError("Uploaded release asset SHA256 mismatch")
    if after["body"] != before["body"]:
        raise ValueError("Release notes changed during asset upload")
    print(f"Published and verified {tag}: run {run_id}, commit {commit}, {digest}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--run-id", type=int, required=True)
    parser.add_argument("--artifact-id", type=int, required=True)
    args = parser.parse_args()
    publish(os.environ["GH_REPO"], args.tag, args.commit, args.run_id, args.artifact_id, Path("release"))


if __name__ == "__main__":
    main()
