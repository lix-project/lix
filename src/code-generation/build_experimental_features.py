from typing import NamedTuple

from common import cxx_literal, generate_file, load_data

KNOWN_KEYS = set([
    'name',
    'internalName',
])

class ExperimentalFeature(NamedTuple):
    name: str
    internal_name: str
    description: str

    def parse(datum):
        unknown_keys = set(datum.keys()) - KNOWN_KEYS
        if unknown_keys:
            raise ValueError('unknown keys', unknown_keys)
        return ExperimentalFeature(
            name = datum['name'],
            internal_name = datum['internalName'],
            description = datum.content,
        )

def main():
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument('--deprecated', action='store_true', help='Generate deprecated features')
    ap.add_argument('--header', help='Path of the declaration header to generate')
    ap.add_argument('--impl-header', help='Path of the implementation header to generate')
    ap.add_argument('--descriptions', help='Path of the description file to generate')
    ap.add_argument('--shortlist', help='Path of the shortlist file to generate')
    ap.add_argument('defs', help='Experimental feature definition files', nargs='+')
    args = ap.parse_args()

    features = load_data(args.defs, ExperimentalFeature.parse)

    generate_file(args.header, features, lambda feature: feature.name, lambda feature:
        f'    {feature.internal_name},\n')
    generate_file(args.impl_header, features, lambda feature: feature.name, lambda feature:
        f'''    {{
        .tag = {"Dep" if args.deprecated else "Xp"}::{feature.internal_name},
        .name = {cxx_literal(feature.name)},
        .description = {cxx_literal(feature.description)},
    }},
''')
    generate_file(args.descriptions, features, lambda feature: feature.name, lambda feature:
        f'''## [`{feature.name}`]{{#{"dp" if args.deprecated else "xp"}-feature-{feature.name}}}

{feature.description}

''')
    generate_file(args.shortlist, features, lambda feature: feature.name, lambda feature:
        f'  - [`{feature.name}`](@docroot@/contributing/{"deprecated" if args.deprecated else "experimental"}-features.md#{"dp" if args.deprecated else "xp"}-feature-{feature.name})\n')

if __name__ == '__main__':
    main()
