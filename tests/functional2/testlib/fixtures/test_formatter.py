from string import Template

import pytest


def test_template_balanced_kwargs(balanced_templater: type[Template]):
    assert balanced_templater("@test_str@").substitute(test_str="valid") == "valid"


def test_template_balanced_multi_use(balanced_templater: type[Template]):
    assert balanced_templater("@test_str@ @test_str@").substitute(test_str="valid") == "valid valid"


def test_balanced_escapement(balanced_templater: type[Template]):
    assert balanced_templater("@@ @@{test} @{test}@").substitute(test="t") == "@ @{test} t"


# And that correct exceptions get thrown
def test_template_balanced_extra_kwarg(balanced_templater: type[Template]):
    with pytest.raises(ExceptionGroup) as excinfo:
        balanced_templater("{test_str}").substitute(test_str="valid", random_str="random")
    assert excinfo.group_contains(KeyError, match=r"random_str")
    assert not excinfo.group_contains(ValueError)


def test_template_balanced_missing_arg(balanced_templater: type[Template]):
    with pytest.raises(KeyError) as e:
        balanced_templater("@TEST@ @MISSING@").substitute(TEST="valid")
    assert e.match("MISSING")
