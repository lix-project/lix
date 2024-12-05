# copy pasta from libutil-support's terminal-code-eater.cc

import enum
import dataclasses


class State(enum.Enum):
    ExpectESC = 1
    ExpectESCSeq = 2
    InCSIParams = 3
    InCSIIntermediates = 4


@dataclasses.dataclass
class TerminalCodeEater:
    state: State = State.ExpectESC

    def feed(self, data: bytes) -> bytes:
        is_param_char = lambda c: c >= 0x30 and c <= 0x3f
        is_intermediate_char = lambda c: c >= 0x20 and c <= 0x2f
        is_final_char = lambda c: c >= 0x40 and c <= 0x7e

        ret = bytearray()
        for c in data:
            match self.state:
                case State.ExpectESC:
                    match c:
                        case 0x1b:  # \e
                            self._transition(State.ExpectESCSeq)
                            continue
                        case 0xd:  # \r
                            continue
                    ret.append(c)
                case State.ExpectESCSeq:
                    match c:
                        # CSI ('[')
                        case 0x5b:
                            self._transition(State.InCSIParams)
                            continue
                        # FIXME(jade): whatever this was, we do not know how to
                        # delimit it, so we just eat the next character and
                        # keep going. Should we actually eat it?
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
                    elif is_intermediate_char(c):
                        self._transition(State.InCSIIntermediates)
                        continue
                    elif is_param_char(c):
                        continue
                    else:
                        raise ValueError(f'Corrupt escape sequence, at {c:x}')
                case State.InCSIIntermediates:
                    if is_final_char(c):
                        self._transition(State.ExpectESC)
                        continue
                    elif is_intermediate_char(c):
                        continue
                    else:
                        raise ValueError(f'Corrupt escape sequence in intermediates, at {c:x}')

        return bytes(ret)

    def _transition(self, new_state: State):
        self.state = new_state


def eat_terminal_codes(s: bytes) -> bytes:
    return TerminalCodeEater().feed(s)
