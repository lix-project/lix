import frontmatter
import pathlib
from collections import defaultdict

def cxx_escape_character(c):
    if ord(c) >= 0x20 and ord(c) < 0x7f and c != '"' and c != '?' and c != '\\':
        return c
    elif c == '\t':
        return r'\t'
    elif c == '\n':
        return r'\n'
    elif c == '\r':
        return r'\r'
    elif c == '"':
        return r'\"'
    elif c == '?':
        return r'\?'
    elif c == '\\':
        return r'\\'
    elif ord(c) <= 0xffff:
        return str.format(r'\u{:04x}', ord(c))
    else:
        return str.format(r'\U{:08x}', ord(c))

def cxx_literal(v):
    if v is None:
        return 'std::nullopt'
    elif isinstance(v, bool) and v == False: # 0 == False
        return 'false'
    elif isinstance(v, bool) and v == True: # 1 == True
        return 'true'
    elif isinstance(v, int):
        return str(v)
    elif isinstance(v, str):
        return ''.join(['"', *(cxx_escape_character(c) for c in v), '"'])
    elif isinstance(v, list):
        return f'{{{", ".join([cxx_literal(item) for item in v])}}}'
    else:
        raise NotImplementedError(f'cannot represent {repr(v)} in C++')

def load_data(defs, parse_function):
    data = []
    for path in defs:
        try:
            datum = frontmatter.load(path)
            data.append((path, parse_function(datum)))
        except Exception as e:
            e.add_note(f'in {path}')
            raise
    return data

def generate_file(path, data, sort_key_function, generate_function):
    if path is not None:
        with open(path, 'w') as out:
            for path, datum in sorted(data, key=lambda pathAndDatum: sort_key_function(pathAndDatum[1])):
                try:
                    out.write(generate_function(datum))
                except Exception as e:
                    e.add_note(f'in {path}')
                    raise
