from typing import List, NamedTuple

from common import cxx_literal, generate_file, load_data

KNOWN_KEYS = set([
    'name',
    'type',
    'constructorArgs',
    'implementation',
    'impure',
    'renameInGlobalScope',
])

class BuiltinConstant(NamedTuple):
    name: str
    type: str
    implementation: str
    impure: bool
    rename_in_global_scope: bool
    documentation: str

    def parse(datum):
        unknown_keys = set(datum.keys()) - KNOWN_KEYS
        if unknown_keys:
            raise Exception('unknown keys', unknown_keys)
        return BuiltinConstant(
            name = datum['name'],
            type = datum['type'],
            implementation = ('{' + ', '.join([f'NewValueAs::{datum["type"]}', *datum['constructorArgs']]) + '}') if 'constructorArgs' in datum else datum['implementation'],
            impure = datum.get('impure', False),
            rename_in_global_scope = datum.get('renameInGlobalScope', True),
            documentation = datum.content,
        )

VALUE_TYPES = {
    'attrs': 'nAttrs',
    'boolean': 'nBool',
    'integer': 'nInt',
    'list': 'nList',
    'null': 'nNull',
    'string': 'nString',
}

HUMAN_TYPES = {
    'attrs': 'set',
    'boolean': 'Boolean',
    'integer': 'integer',
    'list': 'list',
    'null': 'null',
    'string': 'string',
}

def main():
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument('--header', help='Path of the header to generate')
    ap.add_argument('--docs', help='Path of the documentation file to generate')
    ap.add_argument('defs', help='Builtin definition files', nargs='+')
    args = ap.parse_args()

    builtin_constants = load_data(args.defs, BuiltinConstant.parse)

    generate_file(args.header, builtin_constants, lambda constant:
        # `builtins` is magic and must come first
        '' if constant.name == 'builtins' else constant.name,
    lambda constant:
        f'''{'if (!evalSettings.pureEval) ' if constant.impure else ''}{{
    addConstant({cxx_literal(('__' if constant.rename_in_global_scope else '') + constant.name)}, {constant.implementation}, {{
        .type = {VALUE_TYPES[constant.type]},
        .doc = {cxx_literal(constant.documentation)},
        .impureOnly = {cxx_literal(constant.impure)},
    }});
}}
''')
    generate_file(args.docs, builtin_constants, lambda constant: constant.name, lambda constant:
        f'''<dt id="builtins-{constant.name}">
  <a href="#builtins-{constant.name}"><code>{constant.name}</code></a> ({HUMAN_TYPES[constant.type]})
</dt>
<dd>

{constant.documentation}

''' + ('''> **Note**
>
> Not available in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).

''' if constant.impure else '') + '''</dd>

''')

if __name__ == '__main__':
    main()
