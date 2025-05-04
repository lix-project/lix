# copy pasta from libutil-support's terminal-code-eater.cc

import enum
import dataclasses


class State(enum.Enum):
    ExpectESC = 1
    ExpectESCSeq = 2
    InCSIParams = 3
    InCSIIntermediates = 4
    InOSCParams = 5
    InOSCST = 6


@dataclasses.dataclass
class TerminalCodeEater:
    state: State = State.ExpectESC

    def feed(self, data: bytes) -> bytes:
        def is_param_char(char: int) -> bool:
            return 48 <= char <= 63

        def is_intermediate_char(char: int) -> bool:
            return 32 <= char <= 47

        def is_final_char(char: int) -> bool:
            return 64 <= char <= 126

        ret = bytearray()
        for c in data:
            match self.state:
                case State.ExpectESC:
                    match c:
                        case 0x1B:  # \e
                            self._transition(State.ExpectESCSeq)
                            continue
                        case 0xD:  # \r
                            continue
                    ret.append(c)
                case State.ExpectESCSeq:
                    match c:
                        case 0x5B:
                            # corresponds to CSI ('[')
                            self._transition(State.InCSIParams)
                            continue
                        case 0x5D:
                            # corresponds to OSC (']')
                            self._transition(State.InOSCParams)
                            continue
                        # FIXME(jade): whatever this was, we do not know how to
                        #  delimit it, so we just eat the next character and
                        #  keep going. Should we actually eat it?
                        case _:
                            self._transition(State.ExpectESC)
                            continue
                # https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_(Control_Sequence_Introducer)_sequences
                # A CSI sequence is: CSI [\x30-\x3f]* [\x20-\x2f]* [\x40-\x7e]
                #                        ^ params     ^ intermediates ^ final byte
                case State.InCSIParams:
                    if is_final_char(c):
                        self._transition(State.ExpectESC)
                        continue
                    if is_intermediate_char(c):
                        self._transition(State.InCSIIntermediates)
                        continue
                    if is_param_char(c):
                        continue
                    msg = f"Corrupt escape sequence, at {c:x}"
                    raise ValueError(msg)
                case State.InCSIIntermediates:
                    if is_final_char(c):
                        self._transition(State.ExpectESC)
                        continue
                    if is_intermediate_char(c):
                        continue
                    msg = f"Corrupt escape sequence in intermediates, at {c:x}"
                    raise ValueError(msg)
                # An OSC is OSC [\x20-\x7e]* ST per ECMA-48
                # where OSC is \x1b ] and ST is \x1b \.
                case State.InOSCParams:
                    # first part of ST
                    if c == 0x1B:
                        self._transition(State.InOSCST)
                        continue
                    # OSC sequences can be ended by BEL on old xterms
                    if c == 0x07:
                        self._transition(State.ExpectESC)
                        continue
                    if c < 0x20 or c == 0x7F:
                        msg = f"Corrupt OSC sequence, at {c:x}"
                        raise ValueError(msg)
                    # either way, eat it
                    continue
                case State.InOSCST:
                    # ST ends by \
                    if c == 0x5C:  # \
                        self._transition(State.ExpectESC)
                    elif c < 0x20 or c > 0x7E:
                        msg = f"Corrupt OSC sequence in ST, at {c:x}"
                        raise ValueError(msg)
                    else:
                        self._transition(State.InOSCParams)
                    continue

        return bytes(ret)

    def _transition(self, new_state: State):
        self.state = new_state


def eat_terminal_codes(s: bytes) -> bytes:
    return TerminalCodeEater().feed(s)
