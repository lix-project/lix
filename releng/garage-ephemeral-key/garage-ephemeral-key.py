# SPDX-FileCopyrightText: 2024 Jade Lovelace
# SPDX-License-Identifier: MIT
import argparse
import json
import sys
import datetime
import dataclasses
import re
from typing import Any, Literal, Optional
import requests
import os
import logging

log = logging.getLogger(__name__)
log.setLevel(logging.INFO)

fmt = logging.Formatter('{asctime} {levelname} {name}: {message}',
                        datefmt='%b %d %H:%M:%S',
                        style='{')

if not any(isinstance(h, logging.StreamHandler) for h in log.handlers):
    hand = logging.StreamHandler()
    hand.setFormatter(fmt)
    log.addHandler(hand)

API_BASE = os.environ.get('GARAGE_ADMIN_API_BASE', 'http://localhost:3903')
API_KEY = os.environ['GARAGE_ADMIN_TOKEN']


def api(method, endpoint: str, resp_json=True, **kwargs) -> Any:
    log.info('http %s %s', method, endpoint)
    if not endpoint.startswith('https'):
        endpoint = API_BASE + endpoint
    resp = requests.request(method,
                            endpoint,
                            headers={'Authorization': f'Bearer {API_KEY}'},
                            **kwargs)
    resp.raise_for_status()
    if resp_json:
        return resp.json()
    else:
        return resp


@dataclasses.dataclass
class Key:
    name: str
    id: str
    secret_key: Optional[str] = None


@dataclasses.dataclass
class Bucket:
    id: str


def keys() -> list[Key]:
    data: list[dict] = api('GET', '/v1/key?list')
    return [Key(name=k['name'], id=k['id']) for k in data]


def delete_key(key: Key):
    api('DELETE', '/v1/key', resp_json=False, params={'id': key.id})


def create_key(name: str) -> Key:
    resp: dict = api('POST', '/v1/key', json={'name': name})
    return Key(name=resp['name'],
               id=resp['accessKeyId'],
               secret_key=resp['secretAccessKey'])


AccessType = Literal['read'] | Literal['write'] | Literal['owner']


def get_bucket(bucket_name: str) -> Bucket:
    resp: dict = api('GET', '/v1/bucket', params={'globalAlias': bucket_name})
    return Bucket(resp['id'])


def grant(bucket: Bucket, access_types: list[AccessType], key: Key):
    access_types_dict = {k: True for k in access_types}
    api('POST',
        '/v1/bucket/allow',
        json={
            'bucketId': bucket.id,
            'accessKeyId': key.id,
            'permissions': access_types_dict,
        })


KEY_RE = re.compile(r'^.*ephemeral-(\d{14})$')
DATEFMT = '%Y%m%d%H%M%S'


def expired_keys(older_than: datetime.datetime) -> list[Key]:
    ret = []
    for key in keys():
        if m := KEY_RE.match(key.name):
            date = datetime.datetime.strptime(m.group(1), DATEFMT)
            date = date.astimezone(datetime.UTC)
            print(date)
            if date < older_than:
                ret.append(key)
    return ret


def do_new(args):
    buckets = [get_bucket(b) for b in args.buckets]

    def optional(s: str, whether) -> list[str]:
        if whether:
            return [s]
        else:
            return []

    access_types: list[AccessType] = optional('read', args.read) + optional(
        'write', args.write) + optional('owner', args.owner)  # type: ignore

    key_name = args.name + '-' if args.name else ''
    key_name += "ephemeral-" + (
        datetime.datetime.now(tz=datetime.UTC) +
        datetime.timedelta(seconds=args.age_secs)).strftime(DATEFMT)

    k = create_key(key_name)
    for b in buckets:
        grant(b, access_types, k)

    print(json.dumps(dataclasses.asdict(k), indent=2))


def do_clean(args):
    older_than = datetime.datetime.now(tz=datetime.UTC)
    for key in expired_keys(older_than):
        delete_key(key)


def main():
    ap = argparse.ArgumentParser(description="Garage ephemeral API keys tool")

    def fail(*args):
        ap.print_help()
        sys.exit(1)

    ap.set_defaults(cmd=fail)

    sps = ap.add_subparsers()

    new = sps.add_parser("new", help="Make an ephemeral key")
    new.add_argument("--name", help="Name prefix for the key")
    new.add_argument("--read",
                     action="store_true",
                     help="Grant read access to buckets")
    new.add_argument("--write",
                     action="store_true",
                     help="Grant write access to buckets")
    new.add_argument("--owner",
                     action="store_true",
                     help="Grant owner access to buckets")
    new.add_argument("--age-secs",
                     type=int,
                     required=True,
                     help="Maximum key lifetime in seconds")
    new.add_argument("buckets", nargs='*', help="Buckets to grant access to")
    new.set_defaults(cmd=do_new)

    clean = sps.add_parser("clean", help="Clean up old keys")
    clean.set_defaults(cmd=do_clean)

    args = ap.parse_args()
    args.cmd(args)


if __name__ == '__main__':
    main()
