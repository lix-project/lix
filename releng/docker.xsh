from .environment import DockerTarget, RelengEnvironment
from .version import VERSION
from pathlib import Path

def check_all_logins(env: RelengEnvironment):
    for target in env.docker_targets:
        check_login(target)

def check_login(target: DockerTarget):
    skopeo login @(target.registry_name())

def upload_docker_image(target: DockerTarget, path: Path):
    skopeo --insecure-policy copy docker-archive:@(path) docker://@(target.resolve(version=VERSION))
