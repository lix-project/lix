from typing import Callable
import urllib.parse
import re
import functools
import subprocess
import dataclasses

S3_HOST = 's3.lix.systems'

DEFAULT_STORE_URI_BITS = {
    'region': 'garage',
    'endpoint': 's3.lix.systems',
    'want-mass-query': 'true',
    'write-nar-listing': 'true',
    'ls-compression': 'zstd',
    'narinfo-compression': 'zstd',
    'compression': 'zstd',
    'parallel-compression': 'true',
}


@dataclasses.dataclass
class DockerTarget:
    registry_path: str
    """Registry path without the tag, e.g. ghcr.io/lix-project/lix"""

    tags: list[str]
    """List of tags this image should take. There must be at least one."""

    @staticmethod
    def resolve(item: str, version: str, major: str) -> str:
        """
        Applies templates:
        - version: the Lix version e.g. 2.90.0
        - major: the major Lix version e.g. 2.90
        """
        return item.format(version=version, major=major)

    def registry_name(self) -> str:
        [a, _, _] = self.registry_path.partition('/')
        return a


@dataclasses.dataclass
class RelengEnvironment:
    name: str
    colour: Callable[[str], str]

    cache_store_overlay: dict[str, str]
    cache_bucket: str
    releases_bucket: str
    docs_bucket: str
    # We don't want to call functions that require computation (such as guess_gerrit_remote()) before they are needed
    git_repo: Callable[[], str]
    git_repo_is_gerrit: bool
    s3_endpoint: str
    s3_ssh_host: str | None

    docker_targets: list[DockerTarget]

    def cache_store_uri(self):
        qs = DEFAULT_STORE_URI_BITS.copy()
        qs.update(self.cache_store_overlay)
        return self.cache_bucket + "?" + urllib.parse.urlencode(qs)


SGR = '\x1b['
RED = '31;1m'
GREEN = '32;1m'
BLUE = '34;1m'
RESET = '0m'


def sgr(colour: str, text: str) -> str:
    return f'{SGR}{colour}{text}{SGR}{RESET}'


LOCAL = RelengEnvironment(
    name='local',
    colour=functools.partial(sgr, BLUE),
    docs_bucket='s3://local-docs',
    cache_bucket='s3://local-cache',
    cache_store_overlay={'secret-key': 'local.key', 'endpoint': 'localhost:3900', 'scheme': 'http'},
    releases_bucket='s3://local-releases',
    git_repo=lambda: '../releng-target',
    git_repo_is_gerrit=False,
    docker_targets=[],
    s3_endpoint = 'http://localhost:3900',
    s3_ssh_host = None,
)


STAGING = RelengEnvironment(
    name='staging',
    colour=functools.partial(sgr, GREEN),
    docs_bucket='s3://staging-docs',
    cache_bucket='s3://staging-cache',
    cache_store_overlay={'secret-key': 'staging.key'},
    releases_bucket='s3://staging-releases',
    git_repo=lambda: 'ssh://git@git.lix.systems/lix-project/lix-releng-staging',
    git_repo_is_gerrit=False,
    docker_targets=[
        # latest will be auto tagged if appropriate
        DockerTarget('git.lix.systems/lix-project/lix-releng-staging',
                     tags=['{version}', '{major}']),
        DockerTarget('ghcr.io/lix-project/lix-releng-staging',
                     tags=['{version}', '{major}']),
    ],
    s3_endpoint = 'https://s3.lix.systems',
    s3_ssh_host = S3_HOST,
)

GERRIT_REMOTE_RE = re.compile(r'^ssh://(\w+@)?gerrit.lix.systems:2022/lix$')


def guess_gerrit_remote():
    """
    Deals with people having unknown gerrit username.
    """
    out = [
        x.split()[1] for x in subprocess.check_output(
            ['git', 'remote', '-v']).decode().splitlines()
    ]
    return next(x for x in out if GERRIT_REMOTE_RE.match(x))


PROD = RelengEnvironment(
    name='production',
    colour=functools.partial(sgr, RED),
    docs_bucket='s3://docs',
    cache_bucket='s3://cache',
    # FIXME: we should decrypt this with age into a tempdir in the future, but
    # the issue is how to deal with the recipients file. For now, we should
    # just delete it after doing a release.
    cache_store_overlay={'secret-key': 'prod.key'},
    releases_bucket='s3://releases',
    git_repo=guess_gerrit_remote,
    git_repo_is_gerrit=True,
    docker_targets=[
        # latest will be auto tagged if appropriate
        DockerTarget('git.lix.systems/lix-project/lix',
                     tags=['{version}', '{major}']),
        DockerTarget('ghcr.io/lix-project/lix', tags=['{version}', '{major}']),
    ],
    s3_endpoint = 'https://s3.lix.systems',
    s3_ssh_host = S3_HOST,
)

ENVIRONMENTS = {
    'local': LOCAL,
    'staging': STAGING,
    'production': PROD,
}


@dataclasses.dataclass
class S3Credentials:
    name: str
    id: str
    secret_key: str
