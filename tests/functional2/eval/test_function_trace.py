import pytest
from testlib.fixtures.nix import Nix


class TestFunctionTrace:
    @pytest.fixture(autouse=True)
    def setup(self, nix: Nix):
        self.nix = nix

    def call(self, expr: str) -> list[str]:
        trace = (
            self.nix.nix_instantiate(["--trace-function-calls", "--expr", expr]).run().stderr_plain
        )
        return [
            " ".join(line.split(" ")[:-1])
            for line in trace.splitlines()
            if line.startswith("function-trace")
        ]

    def test_try_eval(self):
        assert self.call('builtins.tryEval (throw "example")') == [
            "function-trace entered «string»:1:1 at",
            "function-trace entered «string»:1:19 at",
            "function-trace exited «string»:1:19 at",
            "function-trace exited «string»:1:1 at",
        ]

    @pytest.mark.parametrize(
        "expr",
        [
            "({ x }: x) { }",
            '({ x }: x) { x = "x"; y = "y"; }',
            "(x: y: x + y) 1",
            "(x: x) 1 2",
            "1 2",
        ],
    )
    def test_single_call(self, expr: str):
        assert self.call(expr) == [
            "function-trace entered «string»:1:1 at",
            "function-trace exited «string»:1:1 at",
        ]
