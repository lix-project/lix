import re
from textwrap import dedent

from testlib.fixtures.nix import Nix


def test_regression_9932(nix: Nix):
    cmd = nix.nix(["eval", "--debugger", "--expr", '(_: throw "oh snap") 42'], flake=True)
    cmd.with_stdin(b":env")
    res = cmd.run().expect(1)
    assert "error: oh snap" in res.stderr_plain


def test_debugger_output(nix: Nix):
    expr = dedent("""
        let x.a = 1; in
        with x;
        (_: builtins.seq x.a (throw "oh snap")) x.a
    """)

    res = (
        nix.nix(["eval", "--debugger", "--expr", expr], flake=True)
        .with_stdin(b":env\n")
        .run()
        .expect(1)
    )
    assert "error: oh snap" in res.stderr_plain
    assert re.findall(r"with: .*a", res.stdout_plain)
    assert re.findall(r"static: .*x", res.stdout_plain)


def test_transparent_break(nix: Nix):
    """
    Make sure that adding a call to builtins.break doesn't
    change the result of an expression
    """

    expr = dedent("""
        let
          inherit (builtins)
            attrNames
            break
            elem
            functionArgs
            head
            isAttrs
            isPath
            isFunction
            map
            mapAttrs
            removeAttrs
            toJSON
            typeOf;
        in
        builtins.all (b: b) [
            ((attrNames { a = 5; }) == (attrNames (break { a = 5; })))
            ((elem 5 [1 5]) == (elem 5 (break [1 5])))
            ((elem (2+3) [1 (2+3)]) == (elem (2+3) (break [1 (2+3)])))
            ((functionArgs ({ a }: 5)) == (functionArgs (break ({ a }: 5))))
            ((head [1 2]) == (head (break [1 2])))
            ((isAttrs { a = 5; }) == (isAttrs (break { a = 5; })))
            ((isPath ./.) == (isPath (break ./.)))
            ((isPath ./${".meow"}) == (isPath (break ./${".meow"})))
            ((isFunction (x: x)) == (isFunction (break (x: x))))
            ((map (x: x) [1 5]) == (map (x: x) (break [1 5])))
            ((mapAttrs (n: v: v) { a = 5; }) == (mapAttrs (n: v: v) (break { a = 5; })))
            ((removeAttrs { a = 5; b = 6; } ["a"]) == (removeAttrs (break { a = 5; b = 6; }) ["a"]))
            ((removeAttrs { ab = 5; } [("a"+"b")]) == (removeAttrs { ab = 5; } [(break ("a"+"b"))]))
            ((toJSON { a = 5; }) == (toJSON (break { a = 5; })))
            ((toJSON { a = [(1+2)]; }) == (toJSON { a = break [(1+2)]; }))
            ((typeOf { a = 5; }) == (typeOf (break { a = 5; })))
            ((typeOf (1+2)) == (typeOf (break (1+2))))
        ]
    """)

    res_no_dbg = nix.nix(["eval", "--expr", expr], flake=True).run().expect(0)
    assert "true" in res_no_dbg.stdout_plain

    res_with_dbg = (
        nix.nix(["eval", "--debugger", "--expr", expr], flake=True)
        .with_stdin(b":c\n" * 50)
        .run()
        .expect(0)
    )
    assert "true" in res_with_dbg.stdout_plain
