import dataclasses
import urllib.parse

S3_HOST = 's3.lix.systems'
S3_ENDPOINT = 'https://s3.lix.systems'

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
class RelengEnvironment:
    name: str

    cache_store_overlay: dict[str, str]
    cache_bucket: str
    releases_bucket: str
    docs_bucket: str
    git_repo: str

    def cache_store_uri(self):
        qs = DEFAULT_STORE_URI_BITS.copy()
        qs.update(self.cache_store_overlay)
        return self.cache_bucket + "?" + urllib.parse.urlencode(qs)

STAGING = RelengEnvironment(
    name='staging',
    docs_bucket='s3://staging-docs',
    cache_bucket='s3://staging-cache',
    cache_store_overlay={
        'secret-key': 'staging.key'
    },
    releases_bucket='s3://staging-releases',
    git_repo='ssh://git@git.lix.systems/lix-project/lix-releng-staging',
)


@dataclasses.dataclass
class S3Credentials:
    name: str
    id: str
    secret_key: str
