from typing import Any, Literal, Optional
import re
from pathlib import Path
import json
import dataclasses
import time
from urllib.parse import unquote
import urllib.request
import logging

import requests.auth
import requests
import xdg_base_dirs

log = logging.getLogger(__name__)
log.setLevel(logging.INFO)

DEBUG_REQUESTS = False
if DEBUG_REQUESTS:
    urllib3_logger = logging.getLogger('requests.packages.urllib3')
    urllib3_logger.setLevel(logging.DEBUG)
    urllib3_logger.propagate = True

# So, there is a bunch of confusing stuff happening in this file. The gist of why it's Like This is:
#
# nix2container does not concern itself with tags (reasonably enough):
# https://github.com/nlewo/nix2container/issues/59
#
# This is fine. But then we noticed: docker images don't play nice if you have
# multiple architectures you want to abstract over if you don't do special
# things. Those special things are images with manifests containing multiple
# images.
#
# Docker has a data model vaguely analogous to git: you have higher level
# objects referring to a bunch of content-addressed blobs.
#
# A multiarch image is more or less just a manifest that refers to more
# manifests; in OCI it is an Index.
#
# See the API spec here: https://github.com/opencontainers/distribution-spec/blob/v1.0.1/spec.md#definitions
# And the Index spec here: https://github.com/opencontainers/image-spec/blob/v1.0.1/image-index.md
#
# skopeo doesn't *know* how to make multiarch *manifests*:
# https://github.com/containers/skopeo/issues/1136
#
# There is a tool called manifest-tool that is supposed to do this
# (https://github.com/estesp/manifest-tool) but it doesn't support putting in
# annotations on the outer image, and I *really* didn't want to write golang to
# fix that. Thus, a little bit of homebrew containers code.
#
# Essentially what we are doing in here is splatting a bunch of images into the
# registry without tagging them (with a silly workaround to skopeo issues),
# then simply sending a new composite manifest ourselves.

DockerArchitecture = Literal['amd64'] | Literal['arm64']
MANIFEST_MIME = 'application/vnd.oci.image.manifest.v1+json'
INDEX_MIME = 'application/vnd.oci.image.index.v1+json'


@dataclasses.dataclass(frozen=True, order=True)
class ImageMetadata:
    size: int
    digest: str
    """sha256:SOMEHEX"""


@dataclasses.dataclass(frozen=True, order=True)
class OCIIndexItem:
    """Information about an untagged uploaded image."""

    metadata: ImageMetadata

    architecture: DockerArchitecture

    os: str = 'linux'

    def serialize(self):
        return {
            'mediaType': MANIFEST_MIME,
            'size': self.metadata.size,
            'digest': self.metadata.digest,
            'platform': {
                'architecture': self.architecture,
                'os': self.os,
            }
        }


@dataclasses.dataclass(frozen=True)
class OCIIndex:
    manifests: list[OCIIndexItem]

    annotations: dict[str, str]

    def serialize(self):
        return {
            'schemaVersion': 2,
            'manifests': [item.serialize() for item in sorted(self.manifests)],
            'annotations': self.annotations
        }


@dataclasses.dataclass
class TaggingOperation:
    manifest: OCIIndex
    tags: list[str]
    """Tags this image is uploaded under"""


runtime_dir = xdg_base_dirs.xdg_runtime_dir()
config_dir = xdg_base_dirs.xdg_config_home()

AUTH_FILES = ([runtime_dir / 'containers/auth.json'] if runtime_dir else []) + \
    [config_dir / 'containers/auth.json', Path.home() / '.docker/config.json']


# Copied from Werkzeug https://github.com/pallets/werkzeug/blob/62e3ea45846d06576199a2f8470be7fe44c867c1/src/werkzeug/http.py#L300-L325
def parse_list_header(value: str) -> list[str]:
    """Parse a header value that consists of a list of comma separated items according
    to `RFC 9110 <https://httpwg.org/specs/rfc9110.html#abnf.extension>`__.

    This extends :func:`urllib.request.parse_http_list` to remove surrounding quotes
    from values.

    .. code-block:: python

        parse_list_header('token, "quoted value"')
        ['token', 'quoted value']

    This is the reverse of :func:`dump_header`.

    :param value: The header value to parse.
    """
    result = []

    for item in urllib.request.parse_http_list(value):
        if len(item) >= 2 and item[0] == item[-1] == '"':
            item = item[1:-1]

        result.append(item)

    return result


# https://www.rfc-editor.org/rfc/rfc2231#section-4
_charset_value_re = re.compile(
    r"""
    ([\w!#$%&*+\-.^`|~]*)'  # charset part, could be empty
    [\w!#$%&*+\-.^`|~]*'  # don't care about language part, usually empty
    ([\w!#$%&'*+\-.^`|~]+)  # one or more token chars with percent encoding
    """,
    re.ASCII | re.VERBOSE,
)


# Copied from: https://github.com/pallets/werkzeug/blob/62e3ea45846d06576199a2f8470be7fe44c867c1/src/werkzeug/http.py#L327-L394
def parse_dict_header(value: str) -> dict[str, str | None]:
    """Parse a list header using :func:`parse_list_header`, then parse each item as a
    ``key=value`` pair.

    .. code-block:: python

        parse_dict_header('a=b, c="d, e", f')
        {"a": "b", "c": "d, e", "f": None}

    This is the reverse of :func:`dump_header`.

    If a key does not have a value, it is ``None``.

    This handles charsets for values as described in
    `RFC 2231 <https://www.rfc-editor.org/rfc/rfc2231#section-3>`__. Only ASCII, UTF-8,
    and ISO-8859-1 charsets are accepted, otherwise the value remains quoted.

    :param value: The header value to parse.

    .. versionchanged:: 3.0
        Passing bytes is not supported.

    .. versionchanged:: 3.0
        The ``cls`` argument is removed.

    .. versionchanged:: 2.3
        Added support for ``key*=charset''value`` encoded items.

    .. versionchanged:: 0.9
       The ``cls`` argument was added.
    """
    result: dict[str, str | None] = {}

    for item in parse_list_header(value):
        key, has_value, value = item.partition("=")
        key = key.strip()

        if not has_value:
            result[key] = None
            continue

        value = value.strip()
        encoding: str | None = None

        if key[-1] == "*":
            # key*=charset''value becomes key=value, where value is percent encoded
            # adapted from parse_options_header, without the continuation handling
            key = key[:-1]
            match = _charset_value_re.match(value)

            if match:
                # If there is a charset marker in the value, split it off.
                encoding, value = match.groups()
                assert encoding
                encoding = encoding.lower()

            # A safe list of encodings. Modern clients should only send ASCII or UTF-8.
            # This list will not be extended further. An invalid encoding will leave the
            # value quoted.
            if encoding in {"ascii", "us-ascii", "utf-8", "iso-8859-1"}:
                # invalid bytes are replaced during unquoting
                value = unquote(value, encoding=encoding)

        if len(value) >= 2 and value[0] == value[-1] == '"':
            value = value[1:-1]

        result[key] = value

    return result


def parse_www_authenticate(www_authenticate):
    scheme, _, rest = www_authenticate.partition(' ')
    scheme = scheme.lower()
    rest = rest.strip()

    parsed = parse_dict_header(rest.rstrip('='))
    return parsed


class AuthState:

    def __init__(self, auth_files: list[Path] = AUTH_FILES):
        self.auth_map: dict[str, str] = {}
        for f in auth_files:
            self.auth_map.update(AuthState.load_auth_file(f))
        self.token_cache: dict[str, str] = {}

    @staticmethod
    def load_auth_file(path: Path) -> dict[str, str]:
        if path.exists():
            with path.open() as fh:
                try:
                    json_obj = json.load(fh)
                    auths = json_obj.get('auths', {})
                    return {k: v['auth'] for k, v in auths.items()}
                except (json.JSONDecodeError, KeyError) as e:
                    log.exception('JSON decode error in %s', path, exc_info=e)
        return {}

    def get_token(self, hostname: str) -> Optional[str]:
        return self.token_cache.get(hostname)

    def obtain_token(self, session: requests.Session, token_endpoint: str,
                     scope: str, service: str, image_path: str) -> str:
        authority, _, _ = image_path.partition('/')
        if tok := self.get_token(authority):
            return tok

        creds = self.find_credential_for(image_path)
        if not creds:
            raise ValueError('No credentials available for ' + image_path)

        resp = session.get(token_endpoint,
                           params={
                               'client_id': 'lix-releng',
                               'scope': scope,
                               'service': service,
                           },
                           headers={
                               'Authorization': 'Basic ' + creds
                           }).json()
        token = resp['token']
        self.token_cache[authority] = token
        return token

    def find_credential_for(self, image_path: str):
        trails = image_path.split('/')
        for i in range(len(trails)):
            prefix = '/'.join(trails[:len(trails) - i])
            if prefix in self.auth_map:
                return self.auth_map[prefix]

        return None


class RegistryAuthenticator(requests.auth.AuthBase):
    """Authenticates to an OCI compliant registry"""

    def __init__(self, auth_state: AuthState, session: requests.Session,
                 image: str):
        self.auth_map: dict[str, str] = {}
        self.image = image
        self.session = session
        self.auth_state = auth_state

    def response_hook(self, r: requests.Response,
                      **kwargs: Any) -> requests.Response:
        if r.status_code == 401:
            www_authenticate = r.headers.get('www-authenticate', '').lower()
            parsed = parse_www_authenticate(www_authenticate)
            assert parsed

            tok = self.auth_state.obtain_token(
                self.session,
                parsed['realm'],  # type: ignore
                parsed['scope'],  # type: ignore
                parsed['service'],  # type: ignore
                self.image)

            new_req = r.request.copy()
            new_req.headers['Authorization'] = 'Bearer ' + tok

            return self.session.send(new_req)
        else:
            return r

    def __call__(self,
                 r: requests.PreparedRequest) -> requests.PreparedRequest:
        authority, _, _ = self.image.partition('/')
        auth_may = self.auth_state.get_token(authority)

        if auth_may:
            r.headers['Authorization'] = 'Bearer ' + auth_may

        r.register_hook('response', self.response_hook)
        return r


class Registry:

    def __init__(self, session: requests.Session):
        self.auth_state = AuthState()
        self.session = session

    def image_info(self, image_path: str, manifest_id: str) -> ImageMetadata:
        authority, _, path = image_path.partition('/')
        resp = self.session.head(
            f'https://{authority}/v2/{path}/manifests/{manifest_id}',
            headers={'Accept': MANIFEST_MIME},
            auth=RegistryAuthenticator(self.auth_state, self.session,
                                       image_path))
        resp.raise_for_status()
        return ImageMetadata(int(resp.headers['content-length']),
                             resp.headers['docker-content-digest'])

    def delete_tag(self, image_path: str, tag: str):
        authority, _, path = image_path.partition('/')
        resp = self.session.delete(
            f'https://{authority}/v2/{path}/manifests/{tag}',
            headers={'Content-Type': INDEX_MIME},
            auth=RegistryAuthenticator(self.auth_state, self.session,
                                       image_path))
        resp.raise_for_status()

    def _upload_index(self, image_path: str, tag: str, index: OCIIndex):
        authority, _, path = image_path.partition('/')
        body = json.dumps(index.serialize(),
                          separators=(',', ':'),
                          sort_keys=True)

        resp = self.session.put(
            f'https://{authority}/v2/{path}/manifests/{tag}',
            data=body,
            headers={'Content-Type': INDEX_MIME},
            auth=RegistryAuthenticator(self.auth_state, self.session,
                                       image_path))
        resp.raise_for_status()

        return resp.headers['Location']

    def upload_index(self,
                     image_path: str,
                     tag: str,
                     index: OCIIndex,
                     retries=20,
                     retry_delay=1):
        # eventual consistency lmao
        for _ in range(retries):
            try:
                return self._upload_index(image_path, tag, index)
            except requests.HTTPError as e:
                if e.response.status_code != 404:
                    raise

            time.sleep(retry_delay)
