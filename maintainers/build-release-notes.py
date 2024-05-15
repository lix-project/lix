import frontmatter
import sys
import pathlib
import textwrap

GH_BASE = "https://github.com/NixOS/nix"
FORGEJO_BASE = "https://git.lix.systems/lix-project/lix"
GERRIT_BASE = "https://gerrit.lix.systems/c/lix/+"
KNOWN_KEYS = ('synopsis', 'cls', 'issues', 'prs', 'significance', 'credits')

SIGNIFICANCECES = {
    None: 0,
    'significant': 10,
}

def format_link(ident: str, gh_part: str, fj_part: str) -> str:
    # FIXME: deprecate github as default
    if ident.isdigit():
        num, link, base = int(ident), f"#{ident}", f"{GH_BASE}/{gh_part}"
    elif ident.startswith("gh#"):
        num, link, base = int(ident[3:]), ident, f"{GH_BASE}/{gh_part}"
    elif ident.startswith("fj#"):
        num, link, base = int(ident[3:]), ident, f"{FORGEJO_BASE}/{fj_part}"
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

def run_on_dir(d):
    d = pathlib.Path(d)
    if not d.is_dir():
        raise ValueError(f'provided path {d} is not a directory')
    paths = d.glob('*.md')
    entries = []
    for p in paths:
        try:
            e = frontmatter.load(p)
            if 'synopsis' not in e.metadata:
                raise Exception('missing synopsis')
            unknownKeys = set(e.metadata.keys()) - set(KNOWN_KEYS)
            if unknownKeys:
                raise Exception('unknown keys', unknownKeys)
            entries.append((p, e))
        except Exception as e:
            e.add_note(f"in {p}")
            raise

    def listify(l: list | int) -> list:
        if not isinstance(l, list):
            return [l]
        else:
            return l

    for p, entry in sorted(entries, key=lambda e: (-SIGNIFICANCECES[e[1].metadata.get('significance')], e[0])):
        try:
            header = entry.metadata['synopsis']
            links = []
            links += [format_issue(str(s)) for s in listify(entry.metadata.get('issues', []))]
            links += [format_pr(str(s)) for s in listify(entry.metadata.get('prs', []))]
            links += [format_cl(cl) for cl in listify(entry.metadata.get('cls', []))]
            if links != []:
                header += " " + " ".join(links)
            if header:
                print(f"- {header}")
                print()
            print(textwrap.indent(entry.content, '  '))
            if credits := listify(entry.metadata.get('credits', [])):
                print()
                print(textwrap.indent('Many thanks to {} for this.'.format(plural_list(credits)), '  '))
        except Exception as e:
            e.add_note(f"in {p}")
            raise

if __name__ == '__main__':
    for d in sys.argv[1:]:
        run_on_dir(d)
