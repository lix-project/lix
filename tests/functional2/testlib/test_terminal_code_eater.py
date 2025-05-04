from functional2.testlib.terminal_code_eater import eat_terminal_codes


def test_eats_color():
    assert eat_terminal_codes(b"\x1b[7mfoo blah bar\x1b[0m") == b"foo blah bar"


def test_eats_osc():
    assert (
        eat_terminal_codes(b"\x1b]8;;http://example.com\x1b\\This is a link\x1b]8;;\x1b\\")
        == b"This is a link"
    )
