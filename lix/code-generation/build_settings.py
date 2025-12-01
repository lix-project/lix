import dataclasses
from textwrap import dedent
from typing import Any

from common import (
    cxx_literal,
    generate_file,
    load_data,
    get_experimental_features,
    get_argument_parser,
)


PLATFORM_WARNING = """
  > **Note**
  > This setting is only available on {platforms} systems.

"""

XP_WARNING = """
  > **Warning**
  > This setting is part of an
  > [experimental feature](@docroot@/contributing/experimental-features.md).

  To change this setting, you need to make sure the corresponding experimental feature,
  [`{feature}`](@docroot@/contributing/experimental-features.md#xp-feature-{feature}),
  is enabled.
  For example, include the following in [`nix.conf`](#):

  ```
  extra-experimental-features = {feature}
  {name} = ...
  ```

"""

DEPR_WARNING = """
  > **Warning**
  > This setting is deprecated and will be removed in a future version of Lix.

"""


@dataclasses.dataclass
class Setting:
    name: str
    internal_name: str
    documentation: str

    default_text: str = ""
    setting_type: str = ""
    default_expr: str = ""
    platforms: list[str] = dataclasses.field(default_factory=list)
    aliases: list[str] = dataclasses.field(default_factory=list)
    experimental_feature: str | None = None
    deprecated: bool = False

    default: dataclasses.InitVar[str | None] = None
    type_str: dataclasses.InitVar[str | None] = None

    def __post_init__(self, default: Any, type_str: str | None):
        if default is not None:  # is not None nor an empty String
            self.default_text = f"`{nix_conf_literal(default)}`"
        self.default_expr = self.default_expr or cxx_literal(default)
        self.default_text = self.default_text or "*empty*"

        if type_str is not None:
            self.setting_type = f"Setting<{type_str}>"

    def generate_code(self, experimental_features: dict[str | None, str]) -> str:
        indentation = "    " * 4
        expr = (indent(indentation, self.default_expr) + indentation) if "\n" in self.default_expr else self.default_expr
        return dedent(f"""
            {self.setting_type} {self.internal_name} {{
                this,
                {expr},
                {cxx_literal(self.name)},
                {cxx_literal(self.documentation)},
                {cxx_literal(self.aliases)},
                true,
                {experimental_features[self.experimental_feature]},
                {cxx_literal(self.deprecated)}
            }};
        """)

    @property
    def docs(self) -> str:
        indentation = "    " * 3
        platforms = [p.capitalize() for p in self.platforms]
        aliases = [f"`{item}`" for item in self.aliases]
        description = dedent(f"""

            {indent(indentation, self.documentation)}

            {indent(indentation, PLATFORM_WARNING.format(platforms=str(platforms)[1:-1])) if self.platforms else ""}
            {indent(indentation, XP_WARNING.format(feature=self.experimental_feature, name=self.name)) if self.experimental_feature is not None else ""}
            {indent(indentation, DEPR_WARNING) if self.deprecated else ""}
            **Default:** {self.default_text}
            {f"**Deprecated alias:** {str(aliases)[1:-1]}\n" if self.aliases else ""}
        """)
        return f'- <span id="conf-{self.name}">[`{self.name}`](#conf-{self.name})</span>' + indent(
            "  ",  # indent by two space to make it part of the list point
            description,
        )


platform_names = {"darwin": "Darwin", "linux": "Linux"}


def nix_conf_literal(v: Any) -> str:
    if v is None:
        return ""
    if v is False:
        return "false"
    if v is True:
        return "true"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, str):
        return v
    if isinstance(v, list):
        return " ".join([nix_conf_literal(item) for item in v])
    msg = f"Cannot represent {v!r} in nix.conf"
    raise NotImplementedError(msg)


def indent(prefix: str, body: str) -> str:
    return "".join(["\n" if not line else f"{prefix}{line}\n" for line in body.split("\n")])


def main():
    ap = get_argument_parser()
    ap.add_argument("--kernel", help="Name of the kernel Lix will run on")
    ap.add_argument(
        "--experimental-features", help="Directory containing the experimental feature definitions"
    )
    args = ap.parse_args()

    settings = load_data(args.defs, Setting)

    experimental_features = get_experimental_features(
        args.experimental_features, [s.experimental_feature for (_, s) in settings]
    )

    generate_file(
        args.header,
        settings,
        lambda setting: setting.name,
        lambda setting: setting.generate_code(experimental_features)
        if not setting.platforms or args.kernel in setting.platforms
        else "",
    )
    generate_file(args.docs, settings, lambda setting: setting.name, lambda setting: setting.docs)


if __name__ == "__main__":
    main()
