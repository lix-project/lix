import subprocess
import json
from . import environment


def get_ephemeral_key(
        env: environment.RelengEnvironment) -> environment.S3Credentials:
    output = subprocess.check_output([
        'ssh', '-l', 'root', environment.S3_HOST, 'garage-ephemeral-key',
        'new', '--name', f'releng-{env.name}', '--read', '--write',
        '--age-secs', '3600',
        env.releases_bucket.removeprefix('s3://'),
        env.cache_bucket.removeprefix('s3://'),
        env.docs_bucket.removeprefix('s3://'),
    ])
    d = json.loads(output.decode())
    return environment.S3Credentials(name=d['name'],
                                     id=d['id'],
                                     secret_key=d['secret_key'])
