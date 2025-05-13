from functional2.testlib.fixtures.nix import Nix


def test_err_context(nix: Nix):
    # the lang test framework doesn't check this folder, as there is a custom test in here
    # it won't scream about missing an `in.nix` or .exp files
    result = nix.nix_instantiate(
        ["--show-trace", "--eval", "-E", 'builtins.addErrorContext "Hello" (throw "Foo")']
    ).run()
    assert "Hello" in result.expect(1).stderr_s
