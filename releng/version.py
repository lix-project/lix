import json

version_json = json.load(open('version.json'))
VERSION = version_json['version']
MAJOR = '.'.join(VERSION.split('.')[:2])
RELEASE_NAME = version_json['release_name']
OFFICIAL_RELEASE = version_json['official_release']
