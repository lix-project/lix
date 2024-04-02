#!/usr/bin/env python3

from pathlib import Path
import json
import os, os.path
import sys

name = 'process-docroot.py'

def log(*args, **kwargs):
    kwargs['file'] = sys.stderr
    return print(f'{name}:', *args, **kwargs)

def replace_docroot(relative_md_path: Path, content: str, book_root: Path):
    assert not relative_md_path.is_absolute(), f'{relative_md_path=} from mdbook should be relative'

    md_path_abs = book_root / relative_md_path
    docroot_abs = md_path_abs.parent
    assert docroot_abs.is_dir(), f'supposed docroot {docroot_abs} is not a directory (cwd={os.getcwd()})'

    # The paths mdbook gives us are relative to the directory with book.toml.
    # @docroot@ wants to be replaced with the path relative to `src/`.
    docroot_rel = os.path.relpath(book_root / 'src', start=docroot_abs)

    return content.replace('@docroot@', docroot_rel)

def recursive_replace(data, book_root):
    match data:
        case {'sections': sections}:
            return data | dict(
                sections = [recursive_replace(section, book_root) for section in sections],
            )
        case {'Chapter': chapter}:
            # Path to the .md file for this chapter, relative to book_root.
            path_to_chapter = Path('src') / chapter['path']
            chapter_content = chapter['content']

            return data | dict(
                Chapter = chapter | dict(
                    content = replace_docroot(path_to_chapter, chapter_content, book_root),
                    sub_items = [recursive_replace(sub_item, book_root) for sub_item in chapter['sub_items']],
                ),
            )

        case rest:
            assert False, f'should have been called on a dict, not {type(rest)=}\n\t{rest=}'

def main():

    if len(sys.argv) > 1 and sys.argv[1] == 'supports':
        log('confirming to mdbook that we support their stuff')
        return 0

    # mdbook communicates with us over stdin and stdout.
    # It splorks us a JSON array, the first element describing the context,
    # the second element describing the book itself,
    # and then expects us to send it the modified book JSON over stdout.

    context, book = json.load(sys.stdin)

    # book_root is *not* @docroot@. @docroot@ gets replaced with a relative path to `./src/`.
    # book_root is the directory where book.toml, aka `src`'s parent.
    book_root = Path(context['root'])
    assert book_root.exists(), f'{book_root=} does not exist'
    assert book_root.joinpath('book.toml').is_file(), f'{book_root / "book.toml"} is not a file'

    log('replacing all occurrences of @docroot@ with a relative path')

    # Find @docroot@ in all parts of our recursive book structure.
    replaced_content = recursive_replace(book, book_root)

    replaced_content_str = json.dumps(replaced_content)

    # Give mdbook our changes.
    print(replaced_content_str)

    log('done!')

try:
    sys.exit(main())
except AssertionError as e:
    print(f'{name}: INTERNAL ERROR in mdbook preprocessor', file=sys.stderr)
    print(f'this is a bug in {name}')
    raise
