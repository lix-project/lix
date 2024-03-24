import requests
import textwrap
import dataclasses
import logging
import re
import os

API_BASE = 'https://git.lix.systems/api/v1'
API_KEY = os.environ['FORGEJO_API_KEY']

log = logging.getLogger(__name__)
log.setLevel(logging.INFO)

fmt = logging.Formatter('{asctime} {levelname} {name}: {message}',
                        datefmt='%b %d %H:%M:%S',
                        style='{')

if not any(isinstance(h, logging.StreamHandler) for h in log.handlers):
    hand = logging.StreamHandler()
    hand.setFormatter(fmt)
    log.addHandler(hand)

# These are erring in the direction of re-triage, rather than necessarily
# mapping all metadata of the issue
LABEL_MAPPING = {
    'lix-import': 153, # 'imported',
    'contributor-experience': 148, # 'devx',
    'bug': 150, # 'bug',
    'UX': 149, # 'ux',
    'error-messages': 149, # 'ux',
    'lix-stability': 146, # 'stability',
    'performance': 147, # 'performance',
    'tests': 121, # 'tests',
}

def api(method, endpoint: str, resp_json=True, **kwargs):
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

def paginate(method: str, url: str):
    while True:
        resp = api(method, url, resp_json=False)
        yield from resp.json()
        next_one = resp.links.get('next')
        if not next_one:
            return
        url = next_one.get('url')
        if not url:
            return

class DataClassUnpack:
    """Taken from: https://stackoverflow.com/a/72164665"""
    classFieldCache = {}

    @classmethod
    def instantiate(cls, classToInstantiate, argDict):
        if classToInstantiate not in cls.classFieldCache:
            cls.classFieldCache[classToInstantiate] = {
                f.name
                for f in getattr(classToInstantiate, dataclasses._FIELDS).values() if f._field_type is not dataclasses._FIELD_CLASSVAR # type: ignore
            }

        fieldSet = cls.classFieldCache[classToInstantiate]
        filteredArgDict = {k: v for k, v in argDict.items() if k in fieldSet}
        return classToInstantiate(**filteredArgDict)

@dataclasses.dataclass
class Label:
    name: str
    description: str

@dataclasses.dataclass
class Issue:
    number: int
    url: str
    html_url: str
    title: str
    body: str
    labels: dataclasses.InitVar[list[dict]]
    labels_clean: list[Label] = dataclasses.field(init=False)

    def __post_init__(self, labels):
        self.labels_clean = [DataClassUnpack.instantiate(Label, l) for l in labels]

def issues_to_import():
    yield from paginate('GET', '/repos/nixos/nix/issues?state=open&labels=lix-import')

def issues_already_imported():
    yield from paginate('GET', '/repos/lix-project/lix/issues?state=all&labels=imported')


UPSTREAM_ISSUE_RE = re.compile(r'^Upstream-Issue: https://git\.lix\.systems/NixOS/nix/issues/(\d+)$', re.MULTILINE)

def make_already_imported():
    d = {}
    for issue in issues_already_imported():
        iss = DataClassUnpack.instantiate(Issue, issue)
        print(iss)
        match = UPSTREAM_ISSUE_RE.search(iss.body)
        if match:
            d[int(match.group(1))] = iss

    return d

def new_issue(title, body, labels):
    api('POST', '/repos/lix-project/lix/issues', resp_json=True, json={
        'labels': labels,
        'body': body,
        'title': title,
        'dont_notify': True,
    })

already_imported = make_already_imported()

def import_issue(iss: Issue):
    if iss.number in already_imported:
        log.info('Skipping already imported %d', iss.number)
        return
    new_body = textwrap.dedent('''
        Upstream-Issue: {iss}

        {original_body}
    ''').format(iss=iss.html_url, original_body=iss.body)

    new_labels = [LABEL_MAPPING[l.name] for l in iss.labels_clean if l.name in LABEL_MAPPING]

    new_title = '[Nix#{num}] {title}'.format(num=iss.number, title=iss.title)

    log.info('%s', f'create issue with: {new_labels} {new_title} {new_body}')
    new_issue(new_title, new_body, new_labels)

def go():
    log.info('Importing issues!')
    for issue in issues_to_import():
        import_issue(DataClassUnpack.instantiate(Issue, issue))

if __name__ == '__main__':
    go()
