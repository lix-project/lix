import json
import logging
from pathlib import Path
import tempfile

import requests

from .environment import DockerTarget, RelengEnvironment
from .version import VERSION, MAJOR
from . import gitutils
from .docker_assemble import Registry, OCIIndex, OCIIndexItem
from . import docker_assemble

log = logging.getLogger(__name__)
log.setLevel(logging.INFO)

def check_all_logins(env: RelengEnvironment):
    for target in env.docker_targets:
        check_login(target)

def check_login(target: DockerTarget):
    log.info('Checking login for %s', target.registry_name)
    skopeo login @(target.registry_name())

def upload_docker_images(target: DockerTarget, paths: list[Path]):
    if not paths: return

    sess = requests.Session()
    sess.headers['User-Agent'] = 'lix-releng'

    tag_names = [DockerTarget.resolve(tag, version=VERSION, major=MAJOR) for tag in target.tags]

    # latest only gets tagged for the current release branch of Lix
    if not gitutils.is_maintenance_branch('HEAD'):
        tag_names.append('latest')

    meta = {}

    reg = docker_assemble.Registry(sess)
    manifests = []

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        for path in paths:
            digest_file = tmp / (path.name + '.digest')
            tmp_image = tmp / 'tmp-image.tar.gz'

            # insecure-policy: we don't have any signature policy, we are just uploading an image
            #
            # Absurd: we copy it into an OCI image first so we can get the hash
            # we need to upload it untagged, because skopeo has no "don't tag
            # this" option.
            # The reason for this is that forgejo's container registry throws
            # away old versions of tags immediately, so we cannot use a temp
            # tag, and it *does* reduce confusion to not upload tags that
            # should not be used.
            #
            # Workaround for: https://github.com/containers/skopeo/issues/2354
            log.info('skopeo copy to temp oci-archive %s', tmp_image)
            skopeo --insecure-policy copy --format oci --all --digestfile @(digest_file) docker-archive:@(path) oci-archive:@(tmp_image)

            inspection = json.loads($(skopeo inspect oci-archive:@(tmp_image)))

            docker_arch = inspection['Architecture']
            docker_os = inspection['Os']
            meta = inspection['Labels']

            log.info('Pushing image %s for %s to %s', path, docker_arch, target.registry_path)

            digest = digest_file.read_text().strip()
            skopeo --insecure-policy copy --preserve-digests --all oci-archive:@(tmp_image) f'docker://{target.registry_path}@{digest}'

            # skopeo doesn't give us the manifest size directly, so we just ask the registry
            metadata = reg.image_info(target.registry_path, digest)

            manifests.append(OCIIndexItem(metadata=metadata, architecture=docker_arch, os=docker_os))

    log.info('Pushed images to %r, building a bigger and more menacing manifest from %r with metadata %r', target, manifests, meta)
    # send the multiarch manifest to each tag
    index = OCIIndex(manifests=manifests, annotations=meta)
    for tag in tag_names:
        reg.upload_index(target.registry_path, tag, index)
