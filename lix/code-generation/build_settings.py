from typing import NamedTuple, Any

from frontmatter import Post

from build_experimental_features import ExperimentalFeature
from common import cxx_literal, generate_file, load_data
import argparse

KNOWN_KEYS = {
    "name",
    "internalName",
    "platforms",
    "type",
    "settingType",
    "default",
    "defaultExpr",
    "defaultText",
    "aliases",
    "experimentalFeature",
    "deprecated",
}


class Setting(NamedTuple):
    name: str
    internal_name: str
    description: str
    platforms: list[str] | None
    setting_type: str
    default_expr: str
    default_text: str
    aliases: list[str]
    experimental_feature: str | None
    deprecated: bool

    @classmethod
    def parse(cls, datum: Post) -> "Setting":
        unknown_keys = set(datum.keys()) - KNOWN_KEYS
        if unknown_keys:
            msg = f"unknown keys: {unknown_keys!r}"
            raise ValueError(msg)
        default_text = (
            f"`{nix_conf_literal(datum['default'])}`"
            if "default" in datum
            else datum["defaultText"]
        )
        if default_text == "``":
            default_text = "*empty*"
        return Setting(
            name=datum["name"],  # type: ignore
            internal_name=datum["internalName"],  # type: ignore
            description=datum.content,
            platforms=datum.get("platforms", None),  # type: ignore
            setting_type=f"Setting<{datum['type']}>" if "type" in datum else datum["settingType"],
            default_expr=cxx_literal(datum["default"])
            if "default" in datum
            else datum["defaultExpr"],
            default_text=default_text,
            aliases=datum.get("aliases", []),  # type: ignore
            experimental_feature=datum.get("experimentalFeature", None),  # type: ignore
            deprecated=datum.get("deprecated", False),  # type: ignore
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
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", help="Name of the kernel Lix will run on")
    ap.add_argument("--header", help="Path of the header to generate")
    ap.add_argument("--docs", help="Path of the documentation file to generate")
    ap.add_argument(
        "--experimental-features", help="Directory containing the experimental feature definitions"
    )
    ap.add_argument("defs", help="Setting definition files", nargs="+")
    args = ap.parse_args()

    settings = load_data(args.defs, Setting.parse)

    experimental_feature_names = {setting.experimental_feature for (_, setting) in settings}
    experimental_feature_names.discard(None)
    experimental_feature_files = [
        f"{args.experimental_features}/{name}.md" for name in experimental_feature_names
    ]
    experimental_features = load_data(experimental_feature_files, ExperimentalFeature.parse)
    experimental_features = {
        path_and_feature[1].name: f"Xp::{path_and_feature[1].internal_name}"
        for path_and_feature in experimental_features
    }
    experimental_features[None] = "std::nullopt"

    generate_file(
        args.header,
        settings,
        lambda setting: setting.name,
        lambda setting: f"""{setting.setting_type} {setting.internal_name} {{
    this,
    {setting.default_expr},
    {cxx_literal(setting.name)},
    {cxx_literal(setting.description)},
    {cxx_literal(setting.aliases)},
    true,
    {experimental_features[setting.experimental_feature]},
    {cxx_literal(setting.deprecated)}
}};

"""
        if setting.platforms is None or args.kernel in setting.platforms
        else "",
    )
    generate_file(
        args.docs,
        settings,
        lambda setting: setting.name,
        lambda setting: f"""- <span id="conf-{setting.name}">[`{setting.name}`](#conf-{setting.name})</span>

{indent("  ", setting.description)}
"""
        + (
            f"""  > **Note**
  > This setting is only available on {", ".join([platform_names[platform] for platform in setting.platforms])} systems.

"""
            if setting.platforms is not None
            else ""
        )
        + (
            f"""  > **Warning**
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

"""
            if setting.experimental_feature is not None
            else ""
        )
        + (
            """  > **Warning**
  > This setting is deprecated and will be removed in a future version of Lix.

"""
            if setting.deprecated
            else ""
        )
        + f"""  **Default:** {setting.default_text}

"""
        + (
            f"""  **Deprecated alias:** {", ".join([f"`{item}`" for item in setting.aliases])}

"""
            if setting.aliases != []
            else ""
        ),
    )


if __name__ == "__main__":
    main()
