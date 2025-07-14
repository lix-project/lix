#!/usr/bin/env python3
"""
Preprocesses mdbook markdown, primarily for include directives.

The include directive format is as follows:
    {{#include foo/bar/baz.md}}

The content of includes will be indented as much as the directive itself.

Including a generated file (from building Lix; generally for the 'new' CLI):
    {{#include @generated@/foo/bar/baz.md}}
"""

from pathlib import Path
import json
import os, os.path
import sys
import textwrap

name = 'substitute.py'

def log(*args, **kwargs):
    kwargs['file'] = sys.stderr
    return print(f'{name}:', *args, **kwargs)

def remove_prefix_if_present(s: str, prefix: str) -> str | None:
    if s.startswith(prefix):
        return s.removeprefix(prefix)
    else:
        return None

def do_include(content: str, relative_md_path: Path, source_root: Path, search_path: Path):
    assert not relative_md_path.is_absolute(), f'{relative_md_path=} from mdbook should be relative'

    md_path_abs = source_root / relative_md_path
    var_abs = md_path_abs.parent
    assert var_abs.is_dir(), f'supposed directory {var_abs} is not a directory (cwd={os.getcwd()})'

    lines = []
    for l in content.splitlines(keepends=True):
        if remain := remove_prefix_if_present(l.strip(), "{{#include"):
            requested = remain[1:-2]
            # We indent bodies of indent directives by the indent of the
            # directive itself.
            num_leading_indent = len(l) - len(l.lstrip())

            if subpath := remove_prefix_if_present(requested, "@generated@/"):
                included = search_path / Path(subpath)
                requested = included.relative_to(search_path)
            else:
                included = source_root / relative_md_path.parent / requested
                requested = included.resolve().relative_to(source_root)
            assert included.exists(), f"{requested} not found at {included}"

            lines.append(
                textwrap.indent(
                    do_include(
                        included.read_text(),
                        requested,
                        source_root,
                        search_path,
                    ),
                    " " * num_leading_indent
                )
                + "\n"
            )
        else:
            lines.append(l)
    return "".join(lines)

def recursive_replace(data, book_root, search_path):
    match data:
        case {'sections': sections}:
            return data | dict(
                sections = [recursive_replace(section, book_root, search_path) for section in sections],
            )
        case {'Chapter': chapter}:
            path_to_chapter = Path(chapter['path'])
            chapter_content = chapter['content']

            return data | dict(
                Chapter = chapter | dict(
                    # first process includes. this must happen before docroot processing since
                    # mdbook does not see these included files, only the final agglomeration.
                    content = do_include(
                        chapter_content,
                        path_to_chapter,
                        book_root,
                        search_path
                    ).replace(
                        '@docroot@',
                        ("../" * len(path_to_chapter.parent.parts) or "./")[:-1]
                    ).replace(
                        # this replacement is to avoid corrupting the
                        # hacking.md manual section on docroot
                        '@\\docroot\\@',
                        '@docroot@',
                    ),
                    sub_items = [
                        recursive_replace(sub_item, book_root, search_path)
                        for sub_item in chapter['sub_items']
                    ],
                ),
            )

        case rest:
            assert False, f'should have been called on a dict, not {type(rest)=}\n\t{rest=}'

def main():

    if len(sys.argv) > 1 and sys.argv[1] == 'supports':
        return 0

    # mdbook communicates with us over stdin and stdout.
    # It splorks us a JSON array, the first element describing the context,
    # the second element describing the book itself,
    # and then expects us to send it the modified book JSON over stdout.

    context, book = json.load(sys.stdin)

    # book_root is the directory where book contents leave (ie, src/)
    book_root = Path(context['root']) / context['config']['book']['src']

    # includes pointing into @generated@ will look here
    search_path = Path(os.environ['MDBOOK_SUBSTITUTE_SEARCH'])

    # Find @var@ in all parts of our recursive book structure.
    replaced_content = recursive_replace(book, book_root, search_path)

    replaced_content_str = json.dumps(replaced_content)

    # Give mdbook our changes.
    print(replaced_content_str)

try:
    sys.exit(main())
except AssertionError as e:
    print(f'{name}: INTERNAL ERROR in mdbook preprocessor', file=sys.stderr)
    print(f'this is a bug in {name}', file=sys.stderr)
    raise
