from collections import defaultdict
import frontmatter
import pathlib
import textwrap
from typing import Any, Tuple
import dataclasses
import yaml

GH_ROOT = "https://github.com/"
GH_REPO_BASE = "https://github.com/NixOS/nix"
FORGEJO_REPO_BASE = "https://git.lix.systems/lix-project/lix"
FORGEJO_ROOT = "https://git.lix.systems/"
GERRIT_BASE = "https://gerrit.lix.systems/c/lix/+"
KNOWN_KEYS = ('synopsis', 'cls', 'issues', 'prs', 'significance', 'category', 'credits')

SIGNIFICANCECES = {
    None: 0,
    'significant': 10,
}

# This is just hardcoded for better validation. If you think there should be
# more of them, feel free to add more.
#
# Please update doc/manual/src/contributing/hacking.md if you do. Thanks~
CATEGORIES = [
    'Breaking Changes',
    'Features',
    'Improvements',
    'Fixes',
    'Packaging',
    'Development',
    'Miscellany',
]


@dataclasses.dataclass
class AuthorInfo:
    name: str
    github: str | None = None
    forgejo: str | None = None
    display_name: str | None = None

    def show_name(self) -> str:
        return self.display_name or self.name

    def __str__(self) -> str:
        if self.forgejo:
            return f'[{self.show_name()}]({FORGEJO_ROOT}{self.forgejo})'
        elif self.github:
            return f'[{self.show_name()}]({GH_ROOT}{self.github})'
        else:
            return self.show_name()


class AuthorInfoDB:
    def __init__(self, author_info: dict[str, dict], throw_on_missing: bool):
        self.author_info = {name: AuthorInfo(name=name, **d) for (name, d) in author_info.items()}
        self.throw_on_missing = throw_on_missing

    def __getitem__(self, name) -> str:
        if name in self.author_info:
            return str(self.author_info[name])
        else:
            if self.throw_on_missing:
                raise Exception(f'Missing author info for author {name}')
            else:
                return name


def format_link(ident: str, gh_part: str, fj_part: str) -> str:
    # FIXME: deprecate github as default
    if ident.isdigit():
        raise ValueError('Unprefixed issue numbers are disallowed until the Lix 2.95 cycle, when they will become Forgejo references instead of their previous state of CppNix GitHub issues.'
            '\nIf you mean a Lix Forgejo reference, prefix with fj# or lix#'
            '\nIf you mean a CppNix issue, prefix the number with gh# or nix#')
    elif ident.startswith("gh#"):
        num, link, base = int(ident[3:]), ident, f"{GH_REPO_BASE}/{gh_part}"
    elif ident.startswith("nix#"):
        num, link, base = int(ident[4:]), ident, f"{GH_REPO_BASE}/{gh_part}"
    elif ident.startswith("fj#"):
        num, link, base = int(ident[3:]), ident, f"{FORGEJO_REPO_BASE}/{fj_part}"
    elif ident.startswith("lix#"):
        num, link, base = int(ident[4:]), ident, f"{FORGEJO_REPO_BASE}/{fj_part}"
    else:
        raise Exception("unrecognized reference format", ident)
    return f"[{link}]({base}/{num})"

def format_issue(issue: str) -> str:
    return format_link(issue, "issues", "issues")
def format_pr(pr: str) -> str:
    return format_link(pr, "pull", "pulls")
def format_cl(clid: int) -> str:
    return f"[cl/{clid}]({GERRIT_BASE}/{clid})"

def plural_list(strs: list[str]) -> str:
    if len(strs) <= 1:
        return ''.join(strs)
    else:
        comma = ',' if len(strs) >= 3 else ''
        return '{}{} and {}'.format(', '.join(strs[:-1]), comma, strs[-1])

def listify(l: list | int) -> list:
    if not isinstance(l, list):
        return [l]
    else:
        return l

def do_category(author_info: AuthorInfoDB, entries: list[Tuple[pathlib.Path, Any]]):
    for p, entry in sorted(entries, key=lambda e: (-SIGNIFICANCECES[e[1].metadata.get('significance')], e[0])):
        try:
            header = entry.metadata['synopsis']
            links = []
            links += [format_issue(str(s)) for s in listify(entry.metadata.get('issues', []))]
            links += [format_pr(str(s)) for s in listify(entry.metadata.get('prs', []))]
            links += [format_cl(int(cl)) for cl in listify(entry.metadata.get('cls', []))]
            if links != []:
                header += " " + " ".join(links)

            if header:
                print(f"- {header}")
                print()
            else:
                print("- ", end='')

            print(textwrap.indent(entry.content, '  '))
            if credits := listify(entry.metadata.get('credits', [])):
                print()
                print(textwrap.indent('Many thanks to {} for this.'.format(plural_list(list(author_info[c] for c in credits))), '  '))

            # Blank line after each entry.
            print()
        except Exception as e:
            e.add_note(f"in {p}")
            raise


def run_on_dir(author_info: AuthorInfoDB, d):
    d = pathlib.Path(d)
    if not d.is_dir():
        raise ValueError(f'provided path {d} is not a directory')
    paths = pathlib.Path(d).glob('[!.]*.md')
    entries = defaultdict(list)
    for p in paths:
        try:
            e = frontmatter.load(p) # type: ignore
            if 'synopsis' not in e.metadata:
                raise Exception('missing synopsis')
            unknownKeys = set(e.metadata.keys()) - set(KNOWN_KEYS)
            if unknownKeys:
                raise Exception('unknown keys', unknownKeys)
            category = e.metadata.get('category', 'Miscellany')
            if category not in CATEGORIES:
                raise Exception('unknown category', category)
            entries[category].append((p, e))
        except Exception as e:
            e.add_note(f"in {p}")
            raise

    for category in CATEGORIES:
        if entries[category]:
            print('##', category)
            # Blank line after each heading.
            print()
            do_category(author_info, entries[category])
            # Blank line after each category.
            # This turns into two blank lines when combined with the blank line
            # after each entry.
            print()

def main():
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument('--change-authors', help='File name of the change authors metadata YAML file', type=argparse.FileType('r'))
    ap.add_argument('dirs', help='Directories to run on', nargs='+')

    args = ap.parse_args()

    author_info = AuthorInfoDB(yaml.safe_load(args.change_authors), throw_on_missing=True) \
        if args.change_authors \
        else AuthorInfoDB({}, throw_on_missing=False)

    for d in args.dirs:
        run_on_dir(author_info, d)


if __name__ == '__main__':
    main()
