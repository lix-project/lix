from typing import List, NamedTuple, Optional

from build_experimental_features import ExperimentalFeature
from common import cxx_literal, generate_file, load_data

KNOWN_KEYS = set([
    'name',
    'internalName',
    'platforms',
    'type',
    'settingType',
    'default',
    'defaultExpr',
    'defaultText',
    'aliases',
    'experimentalFeature',
    'deprecated',
])

class Setting(NamedTuple):
    name: str
    internal_name: str
    description: str
    platforms: Optional[List[str]]
    setting_type: str
    default_expr: str
    default_text: str
    aliases: List[str]
    experimental_feature: Optional[str]
    deprecated: bool

    def parse(datum):
        unknown_keys = set(datum.keys()) - KNOWN_KEYS
        if unknown_keys:
            raise ValueError('unknown keys', unknown_keys)
        default_text = f'`{nix_conf_literal(datum["default"])}`' if 'default' in datum else datum['defaultText']
        if default_text == '``':
            default_text = '*empty*'
        return Setting(
            name = datum['name'],
            internal_name = datum['internalName'],
            description = datum.content,
            platforms = datum.get('platforms', None),
            setting_type = f'Setting<{datum["type"]}>' if 'type' in datum else datum['settingType'],
            default_expr = cxx_literal(datum['default']) if 'default' in datum else datum['defaultExpr'],
            default_text = default_text,
            aliases = datum.get('aliases', []),
            experimental_feature = datum.get('experimentalFeature', None),
            deprecated = datum.get('deprecated', False),
        )

platform_names = {
    'darwin': 'Darwin',
    'linux': 'Linux',
}

def nix_conf_literal(v):
    if v is None:
        return ''
    elif isinstance(v, bool) and v == False: # 0 == False
        return 'false'
    elif isinstance(v, bool) and v == True: # 1 == True
        return 'true'
    elif isinstance(v, int):
        return str(v)
    elif isinstance(v, str):
        return v
    elif isinstance(v, list):
        return ' '.join([nix_conf_literal(item) for item in v])
    else:
        raise NotImplementedError(f'Cannot represent {repr(v)} in nix.conf')

def indent(prefix, body):
    return ''.join(['\n' if line == '' else f'{prefix}{line}\n' for line in body.split('\n')])

def main():
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument('--kernel', help='Name of the kernel Lix will run on')
    ap.add_argument('--header', help='Path of the header to generate')
    ap.add_argument('--docs', help='Path of the documentation file to generate')
    ap.add_argument('--experimental-features', help='Directory containing the experimental feature definitions')
    ap.add_argument('defs', help='Setting definition files', nargs='+')
    args = ap.parse_args()

    settings = load_data(args.defs, Setting.parse)

    experimental_feature_names = set([setting.experimental_feature for (_, setting) in settings])
    experimental_feature_names.discard(None)
    experimental_feature_files = [f'{args.experimental_features}/{name}.md' for name in experimental_feature_names]
    experimental_features = load_data(experimental_feature_files, ExperimentalFeature.parse)
    experimental_features = dict(map(lambda path_and_feature:
        (path_and_feature[1].name, f'Xp::{path_and_feature[1].internal_name}'), experimental_features))
    experimental_features[None] = 'std::nullopt'

    generate_file(args.header, settings, lambda setting: setting.name, lambda setting:
        f'''{setting.setting_type} {setting.internal_name} {{
    this,
    {setting.default_expr},
    {cxx_literal(setting.name)},
    {cxx_literal(setting.description)},
    {cxx_literal(setting.aliases)},
    true,
    {experimental_features[setting.experimental_feature]},
    {cxx_literal(setting.deprecated)}
}};

''' if setting.platforms is None or args.kernel in setting.platforms else '')
    generate_file(args.docs, settings, lambda setting: setting.name, lambda setting:
        f'''- <span id="conf-{setting.name}">[`{setting.name}`](#conf-{setting.name})</span>

{indent("  ", setting.description)}
''' + (f'''  > **Note**
  > This setting is only available on {', '.join([platform_names[platform] for platform in setting.platforms])} systems.

''' if setting.platforms is not None else '') + (f'''  > **Warning**
  > This setting is part of an
  > [experimental feature](@docroot@/contributing/experimental-features.md).

  To change this setting, you need to make sure the corresponding experimental feature,
  [`{setting.experimental_feature}`](@docroot@/contributing/experimental-features.md#xp-feature-{setting.experimental_feature}),
  is enabled.
  For example, include the following in [`nix.conf`](#):

  ```
  extra-experimental-features = {setting.experimental_feature}
  {setting.name} = ...
  ```

''' if setting.experimental_feature is not None else '') + ('''  > **Warning**
  > This setting is deprecated and will be removed in a future version of Lix.

''' if setting.deprecated else '') + f'''  **Default:** {setting.default_text}

''' + (f'''  **Deprecated alias:** {', '.join([f'`{item}`' for item in setting.aliases])}

''' if setting.aliases != [] else ''))

if __name__ == '__main__':
    main()
