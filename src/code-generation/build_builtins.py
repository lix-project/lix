from typing import List, NamedTuple, Optional

from build_experimental_features import ExperimentalFeature
from common import cxx_literal, generate_file, load_data

KNOWN_KEYS = set([
    'name',
    'implementation',
    'renameInGlobalScope',
    'args',
    'experimentalFeature',
])

class Builtin(NamedTuple):
    name: str
    implementation: str
    rename_in_global_scope: bool
    args: List[str]
    experimental_feature: Optional[str]
    documentation: str

    def parse(datum):
        unknown_keys = set(datum.keys()) - KNOWN_KEYS
        if unknown_keys:
            raise Exception('unknown keys', unknown_keys)
        return Builtin(
            name = datum['name'],
            implementation = datum['implementation'] if 'implementation' in datum else f'prim_{datum["name"]}',
            rename_in_global_scope = datum.get('renameInGlobalScope', True),
            args = datum['args'],
            experimental_feature = datum.get('experimentalFeature', None),
            documentation = datum.content,
        )

def main():
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument('--header', help='Path of the header to generate')
    ap.add_argument('--docs', help='Path of the documentation file to generate')
    ap.add_argument('--experimental-features', help='Directory containing the experimental feature definitions')
    ap.add_argument('defs', help='Builtin definition files', nargs='+')
    args = ap.parse_args()

    builtins = load_data(args.defs, Builtin.parse)

    experimental_feature_names = set([builtin.experimental_feature for (_, builtin) in builtins])
    experimental_feature_names.discard(None)
    experimental_feature_files = [f'{args.experimental_features}/{name}.md' for name in experimental_feature_names]
    experimental_features = load_data(experimental_feature_files, ExperimentalFeature.parse)
    experimental_features = dict(map(lambda path_and_feature:
        (path_and_feature[1].name, f'Xp::{path_and_feature[1].internal_name}'), experimental_features))
    experimental_features[None] = 'std::nullopt'

    generate_file(args.header, builtins, lambda builtin: builtin.name, lambda builtin:
        f'''{'' if builtin.experimental_feature is None else f'if (experimentalFeatureSettings.isEnabled({experimental_features[builtin.experimental_feature]})) '}{{
    addPrimOp({{
        .name = {cxx_literal(('__' if builtin.rename_in_global_scope else '') + builtin.name)},
        .args = {cxx_literal(builtin.args)},
        .arity = {len(builtin.args)},
        .doc = {cxx_literal(builtin.documentation)},
        .fun = {builtin.implementation},
        .experimentalFeature = {experimental_features[builtin.experimental_feature]},
    }});
}}
''')
    generate_file(args.docs, builtins, lambda builtin: builtin.name, lambda builtin:
        f'''<dt id="builtins-{builtin.name}">
  <a href="#builtins-{builtin.name}"><code>{builtin.name} {' '.join([f'<var>{arg}</var>' for arg in builtin.args])}</code></a>
</dt>
<dd>

{builtin.documentation}

''' + (f'''This function is only available if the [{builtin.experimental_feature}](@docroot@/contributing/experimental-features.md#xp-feature-{builtin.experimental_feature}) experimental feature is enabled.

''' if builtin.experimental_feature is not None else '') + '''</dd>

''')

if __name__ == '__main__':
    main()
