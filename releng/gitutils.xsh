import subprocess
import json


def version_compare(v1: str, v2: str):
    return json.loads($(nix-instantiate --eval --json --argstr v1 @(v1) --argstr v2 @(v2) --expr '{v1, v2}: builtins.compareVersions v1 v2'))


def latest_tag_on_branch(branch: str) -> str:
    return $(git describe --abbrev=0 @(branch) e>/dev/null).strip()


def is_maintenance_branch(branch: str) -> bool:
    try:
        main_tag = latest_tag_on_branch('main')
        current_tag = latest_tag_on_branch(branch)

        return version_compare(current_tag, main_tag) < 0
    except subprocess.CalledProcessError:
        # This is the case before Lix releases 2.90, since main *has* no
        # release tag on it.
        # FIXME: delete this case after 2.91
        return False


def verify_are_on_tag():
    current_tag = $(git describe --tag).strip()
    assert current_tag == VERSION


def git_preconditions():
    # verify there is nothing in index ready to stage
    proc = !(git diff-index --quiet --cached HEAD --)
    assert proc.rtn == 0
    # verify there is nothing *stageable* and tracked
    proc = !(git diff-files --quiet)
    assert proc.rtn == 0
