import dataclasses
from enum import Enum
from textwrap import dedent
from typing import ClassVar, NamedTuple

from common import cxx_literal, generate_file, load_data, get_argument_parser


class FeatureTypeNames(NamedTuple):
    code_tag: str
    doc_tag: str


class FeatureType(FeatureTypeNames, Enum):
    experimental = FeatureTypeNames("Xp", "xp")
    deprecated = FeatureTypeNames("Dep", "dp")


@dataclasses.dataclass
class ExtraFeature:
    name: str
    internal_name: str
    documentation: str

    type: ClassVar[FeatureType]

    @property
    def code(self) -> str:
        return dedent(f"""
            {{
                .tag = {ExtraFeature.type.code_tag}::{self.internal_name},
                .name = {cxx_literal(self.name)},
                .description = {cxx_literal(self.documentation)},
            }},
        """)

    @property
    def docs(self) -> str:
        return dedent(f"""
            ## [`{self.name}`]{{#{ExtraFeature.type.doc_tag}-feature-{self.name}}}

            {self.documentation}

        """)

    @property
    def short_docs(self) -> str:
        return f"  - [`{self.name}`](@docroot@/contributing/{ExtraFeature.type.name}-features.md#{ExtraFeature.type.doc_tag}-feature-{self.name})\n"


def main():
    ap = get_argument_parser()
    ap.add_argument("--deprecated", action="store_true", help="Generate deprecated features")
    ap.add_argument("--impl-header", help="Path of the implementation header to generate")
    ap.add_argument("--shortlist", help="Path of the shortlist file to generate")
    args = ap.parse_args()

    ExtraFeature.type = FeatureType.deprecated if args.deprecated else FeatureType.experimental

    features = load_data(args.defs, ExtraFeature)

    generate_file(
        args.header,
        features,
        lambda feature: feature.name,
        lambda feature: f"    {feature.internal_name},\n",
    )
    generate_file(args.impl_header, features, lambda feature: feature.name, lambda f: f.code)
    generate_file(args.docs, features, lambda feature: feature.name, lambda f: f.docs)
    generate_file(args.shortlist, features, lambda feature: feature.name, lambda f: f.short_docs)


if __name__ == "__main__":
    main()
