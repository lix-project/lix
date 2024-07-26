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

            inspection = json.loads($(skopeo inspect docker-archive:@(path)))

            docker_arch = inspection['Architecture']
            docker_os = inspection['Os']
            meta = inspection['Labels']

            log.info('Pushing image %s for %s to %s', path, docker_arch, target.registry_path)

            # insecure-policy: we don't have any signature policy, we are just uploading an image
            skopeo --insecure-policy copy --digestfile @(digest_file) --all docker-archive:@(path) f'docker://{target.registry_path}@@unknown-digest@@'
            digest = digest_file.read_text().strip()

            # skopeo doesn't give us the manifest size directly, so we just ask the registry
            metadata = reg.image_info(target.registry_path, digest)

            manifests.append(OCIIndexItem(metadata=metadata, architecture=docker_arch, os=docker_os))

    log.info('Pushed images to %r, building a bigger and more menacing manifest from %r with metadata %r', target, manifests, meta)
    # send the multiarch manifest to each tag
    index = OCIIndex(manifests=manifests, annotations=meta)
    for tag in tag_names:
        reg.upload_index(target.registry_path, tag, index)
