import dataclasses
from enum import Enum
from textwrap import dedent, indent
from typing import NamedTuple


from common import cxx_literal, generate_file, load_data, get_argument_parser

IMPURE_NOTE = """
> **Note**
>
> Not available in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).

"""


class TypeName(NamedTuple):
    human: str
    code: str


class BuiltinType(TypeName, Enum):
    attrs = TypeName("set", "nAttrs")
    boolean = TypeName("boolean", "nBool")
    integer = TypeName("integer", "nInt")
    list = TypeName("list", "nList")
    null = TypeName("null", "nNull")
    string = TypeName("string", "nString")

    @classmethod
    def from_string(cls, t_name: str) -> "BuiltinType":
        for t in cls:
            if t_name == t.name:
                return t
        msg = f"Invalid builtin type: {t_name}"
        raise ValueError(msg)


@dataclasses.dataclass
class BuiltinConstant:
    name: str
    documentation: str

    # Fields with different name in the Post than in here
    # our fields
    type: BuiltinType = dataclasses.field(init=False)

    # Post fields
    type_str: dataclasses.InitVar[str]
    constructor_args: dataclasses.InitVar[list[str] | None] = None

    implementation: str = ""
    impure: bool = False
    rename_in_global_scope: bool = True

    def __post_init__(self, type_str: str, constructor_args: list[str] | None):
        self.type = BuiltinType.from_string(type_str)
        if constructor_args is not None:
            args = [f"NewValueAs::{type_str}"] + constructor_args
            self.implementation = f"{{{','.join(args)}}}"

    @property
    def code(self) -> str:
        cond = "if (!evalSettings.pureEval) " if self.impure else ""
        return dedent(f"""
            {cond} {{
                addConstant(
                    {cxx_literal(("__" if self.rename_in_global_scope else "") + self.name)},
                    {self.implementation},
                    {{
                        .type = {self.type.code},
                        .doc = {cxx_literal(self.documentation)},
                        .impureOnly = {cxx_literal(self.impure)},
                    }}
                );
            }}
        """)

    @property
    def docs(self) -> str:
        indentation = "    " * 3
        return dedent(f"""
            <dt id="builtins-{self.name}">
              <a href="#builtins-{self.name}"><code>{self.name}</code></a> ({self.type.human})
            </dt>
            <dd>

            {indent(self.documentation, indentation)}
            {indent(IMPURE_NOTE, indentation) if self.impure else ""}

            </dd>

        """)


def main():
    args = get_argument_parser().parse_args()

    builtin_constants = load_data(args.defs, BuiltinConstant)

    generate_file(
        args.header,
        builtin_constants,
        lambda constant:
        # `builtins` is magic and must come first
        "" if constant.name == "builtins" else constant.name,
        lambda b: b.code,
    )
    generate_file(args.docs, builtin_constants, lambda constant: constant.name, lambda b: b.docs)


if __name__ == "__main__":
    main()
