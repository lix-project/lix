import frontmatter
import sys
import pathlib
import textwrap

GH_BASE = "https://github.com/NixOS/nix"
FORGEJO_BASE = "https://git.lix.systems/lix-project/lix"
GERRIT_BASE = "https://gerrit.lix.systems/c/lix/+"

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
def format_cl(cl: str) -> str:
    clid = int(cl)
    return f"[cl/{clid}]({GERRIT_BASE}/{clid})"

paths = pathlib.Path(sys.argv[1]).glob('*.md')
entries = []
for p in paths:
    try:
        e = frontmatter.load(p)
        if 'synopsis' not in e.metadata:
            raise Exception('missing synposis')
        unknownKeys = set(e.metadata.keys()) - set(('synopsis', 'cls', 'issues', 'prs', 'significance'))
        if unknownKeys:
            raise Exception('unknown keys', unknownKeys)
        entries.append((p, e))
    except Exception as e:
        e.add_note(f"in {p}")
        raise

for p, entry in sorted(entries, key=lambda e: (-SIGNIFICANCECES[e[1].metadata.get('significance')], e[0])):
    try:
        header = entry.metadata['synopsis']
        links = []
        links += map(format_issue, str(entry.metadata.get('issues', "")).split())
        links += map(format_pr, str(entry.metadata.get('prs', "")).split())
        links += map(format_cl, str(entry.metadata.get('cls', "")).split())
        if links != []:
            header += " " + " ".join(links)
        if header:
            print(f"- {header}")
            print()
        print(textwrap.indent(entry.content, '  '))
        print()
    except Exception as e:
        e.add_note(f"in {p}")
        raise
