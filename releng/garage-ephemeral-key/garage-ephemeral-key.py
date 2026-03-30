# SPDX-FileCopyrightText: 2024 Jade Lovelace
# SPDX-FileCopyrightText: 2026 Yureka Lilian <yureka@cyberchaos.dev>
# SPDX-License-Identifier: MIT
import argparse
import json
import sys
import datetime
import re
from typing import Any
import requests
import os
import logging

log = logging.getLogger(__name__)
log.setLevel(logging.INFO)

fmt = logging.Formatter(
    "{asctime} {levelname} {name}: {message}",
    datefmt="%b %d %H:%M:%S",
    style="{",
)

if not any(isinstance(h, logging.StreamHandler) for h in log.handlers):
    hand = logging.StreamHandler()
    hand.setFormatter(fmt)
    log.addHandler(hand)

API_BASE = os.environ.get("GARAGE_ADMIN_API_BASE", "http://localhost:3903")
API_KEY = os.environ["GARAGE_ADMIN_TOKEN"]

BUCKET_REGEX_STR = os.environ.get("BUCKET_REGEX", ".*")
BUCKET_REGEX = re.compile(BUCKET_REGEX_STR)


def api(method, endpoint: str, resp_json=True, **kwargs) -> Any:
    log.info("http %s %s", method, endpoint)
    if not endpoint.startswith("https"):
        endpoint = API_BASE + endpoint
    resp = requests.request(
        method,
        endpoint,
        headers={"Authorization": f"Bearer {API_KEY}"},
        **kwargs,
    )
    resp.raise_for_status()
    if resp_json:
        return resp.json()
    else:
        return resp


def get_bucket_id(bucket_name: str) -> str:
    resp: dict = api(
        "GET", "/v2/GetBucketInfo", params={"globalAlias": bucket_name}
    )
    return resp["id"]


DATEFMT = "%Y%m%d%H%M%S"


def do_new(args):
    for b in args.buckets:
        if not BUCKET_REGEX.match(b):
            print(f"Bucket {b} not in allowed buckeds '{BUCKET_REGEX_STR}'")
            exit(1)
    bucket_ids = [get_bucket_id(b) for b in args.buckets]

    key_name = args.name + "-" if args.name else ""
    expiration = datetime.datetime.now(tz=datetime.UTC) + datetime.timedelta(
        seconds=args.age_secs
    )
    key_name += "ephemeral-" + expiration.strftime(DATEFMT)

    key_resp: dict = api(
        "POST",
        "/v2/CreateKey",
        json={
            "name": key_name,
            "expiration": expiration.isoformat(),
            "neverExpires": False,
        },
    )

    for b in bucket_ids:
        api(
            "POST",
            "/v2/AllowBucketKey",
            json={
                "accessKeyId": key_resp["accessKeyId"],
                "bucketId": b,
                "permissions": {
                    "read": args.read,
                    "write": args.write,
                    "owner": args.owner,
                },
            },
        )

    print(
        json.dumps(
            {
                "name": key_resp["name"],
                "id": key_resp["accessKeyId"],
                "secret_key": key_resp["secretAccessKey"],
            },
            indent=2,
        )
    )


def main():
    ap = argparse.ArgumentParser(description="Garage ephemeral API keys tool")

    def fail(*args):
        ap.print_help()
        sys.exit(1)

    ap.set_defaults(cmd=fail)

    sps = ap.add_subparsers()

    new = sps.add_parser("new", help="Make an ephemeral key")
    new.add_argument("--name", help="Name prefix for the key")
    new.add_argument(
        "--read", action="store_true", help="Grant read access to buckets"
    )
    new.add_argument(
        "--write", action="store_true", help="Grant write access to buckets"
    )
    new.add_argument(
        "--owner", action="store_true", help="Grant owner access to buckets"
    )
    new.add_argument(
        "--age-secs",
        type=int,
        required=True,
        help="Maximum key lifetime in seconds",
    )
    new.add_argument("buckets", nargs="*", help="Buckets to grant access to")
    new.set_defaults(cmd=do_new)

    args = ap.parse_args()
    args.cmd(args)


if __name__ == "__main__":
    main()
