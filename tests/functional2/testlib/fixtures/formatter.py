import string
from string import Template

import pytest


class BalancedTemplater(Template):
    delimiter = "@"
    pattern = r"@((?P<escaped>@)|(?P<named>\w+?)@|\{(?P<braced>\w+?)\}@|(?P<invalid>.*?))"

    def substitute(self, mapping: dict[str, object] | None = None, /, **kwargs) -> str:
        if mapping is None:
            mapping = {}
        tmpl_idents = set(self.get_identifiers())
        subs_idents = mapping.keys() | kwargs.keys()
        errs = []
        for key in subs_idents - tmpl_idents:
            msg = f"Unused named argument `{key}` with value: {kwargs.get(key, mapping.get(key))}"
            errs.append(KeyError(msg))
        if len(errs) > 0:
            msg = "Unused arguments passed to substitute"
            raise ExceptionGroup(msg, errs)
        return super().substitute(mapping, **kwargs)


@pytest.fixture
def balanced_templater() -> type[string.Template]:
    return BalancedTemplater
