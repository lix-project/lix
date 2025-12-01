import dataclasses
from textwrap import dedent, indent

from common import (
    cxx_literal,
    generate_file,
    load_data,
    get_argument_parser,
    get_experimental_features,
)


@dataclasses.dataclass
class Builtin:
    name: str
    documentation: str
    args: list[str]
    experimental_feature: str | None = None
    implementation: str = ""
    rename_in_global_scope: bool = True

    def __post_init__(self):
        self.implementation = self.implementation or f"prim_{self.name}"

    def generate_code(self, experimental_features: dict[str, str]) -> str:
        xf = experimental_features[self.experimental_feature]
        cond = (
            f"if (experimentalFeatureSettings.isEnabled({xf})) "
            if self.experimental_feature
            else ""
        )
        return dedent(f"""
        {cond}{{
            addPrimOp({{
                .name = {cxx_literal(("__" if self.rename_in_global_scope else "") + self.name)},
                .args = {cxx_literal(self.args)},
                .arity = {len(self.args)},
                .doc = {cxx_literal(self.documentation)},
                .fun = {self.implementation},
                .experimentalFeature = {xf},
            }});
        }}
        """)

    @property
    def docs(self) -> str:
        return dedent(f"""
            <dt id="builtins-{self.name}">
              <a href="#builtins-{self.name}"><code>{self.name} {
            " ".join([f"<var>{arg}</var>" for arg in self.args])
        }</code></a>
            </dt>
            <dd>

            {indent(self.documentation, "    " * 3)}

            {
            f"This function is only available if the [{self.experimental_feature}](@docroot@/contributing/experimental-features.md#xp-feature-{self.experimental_feature}) experimental feature is enabled."
            if self.experimental_feature is not None
            else ""
        }
            </dd>

        """)


def main():
    ap = get_argument_parser()
    ap.add_argument(
        "--experimental-features", help="Directory containing the experimental feature definitions"
    )
    args = ap.parse_args()

    builtins = load_data(args.defs, Builtin)

    experimental_features = get_experimental_features(
        args.experimental_features, [b.experimental_feature for (_, b) in builtins]
    )

    generate_file(
        args.header,
        builtins,
        lambda builtin: builtin.name,
        lambda b: b.generate_code(experimental_features),
    )
    generate_file(args.docs, builtins, lambda builtin: builtin.name, lambda b: b.docs)


if __name__ == "__main__":
    main()
