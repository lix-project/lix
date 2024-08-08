import subprocess
from packaging.version import Version

from .version import VERSION


def remote_is_plausible(url: str) -> bool:
    return ('git.lix.systems' in url and 'lix-project/lix' in url) or ('gerrit.lix.systems' in url and url.endswith('lix'))


def version_compare(v1: str, v2: str):
    v1 = Version(v1)
    v2 = Version(v2)
    if v1 < v2:
        return -1
    elif v1 > v2:
        return 1
    elif v1 == v2:
        return 0
    else:
        raise ValueError('these versions are beyond each others celestial plane')


def latest_tag_on_branch(branch: str) -> str:
    return $(git describe --abbrev=0 @(branch) e>/dev/null).strip()


def is_maintenance_branch(branch: str) -> bool:
    """
    Returns whether the given branch is probably a maintenance branch.

    This uses a heuristic: `main` should have a newer tag than a given
    maintenance branch if there has been a major release since that maintenance
    branch.
    """
    assert remote_is_plausible($(git remote get-url origin).strip())
    main_tag = latest_tag_on_branch('origin/main')
    current_tag = latest_tag_on_branch(branch)

    return version_compare(current_tag, main_tag) < 0


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
