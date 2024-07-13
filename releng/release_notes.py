from pathlib import Path
import subprocess
import datetime

from .version import MAJOR, VERSION, RELEASE_NAME

MANUAL = Path('doc/manual')
RELEASE_NOTES_BASE = MANUAL / 'src/release-notes'
VERSION_RL = RELEASE_NOTES_BASE / f'rl-{MAJOR}.md'
SUMMARY = MANUAL / 'src/SUMMARY.md'

def add_to_summary(date: str):
    # N.B: This kind of duplicates gitutils.is_maintenance_branch, but it's a more clear
    # check that allows potentially releasing a -rc without release notes being
    # moved, then in .0 actually move the release notes in place.
    if VERSION_RL.exists():
        return

    MARKER = '  <!-- RELENG-AUTO-INSERTION-MARKER'

    new_lines = []
    for line in SUMMARY.read_text().splitlines():
        new_lines.append(line)
        if MARKER in line:
            indent, _, _ = line.partition(MARKER)
            new_lines.append(f'{indent}- [Lix {MAJOR} ({date})](release-notes/rl-{MAJOR}.md)')

    # make pre-commit happy about one newline
    text = '\n'.join(new_lines).rstrip()
    text += '\n'
    SUMMARY.write_text(text)

def build_release_notes_to_file():
    date = datetime.datetime.now().strftime('%Y-%m-%d')
    add_to_summary(date)

    print('[+] Preparing release notes')
    RELEASE_NOTES_PATH = Path('doc/manual/rl-next')

    if RELEASE_NOTES_PATH.is_dir():
        notes_body = subprocess.check_output(['build-release-notes', '--change-authors', 'doc/manual/change-authors.yml', RELEASE_NOTES_PATH]).decode()
    else:
        # I guess nobody put release notes on their changes?
        print('[-] Warning: seemingly missing any release notes, not worrying about it')
        notes_body = ''

    rl_path = Path(RELEASE_NOTES_BASE / f'rl-{MAJOR}.md')

    existing_rl = ''
    try:
        with open(rl_path, 'r') as fh:
            existing_rl = fh.read()
    except FileNotFoundError:
        pass

    minor_header = f'# Lix {VERSION} ({date})'

    header = f'# Lix {MAJOR} "{RELEASE_NAME}"'
    if existing_rl.startswith(header):
        # strip the header off for minor releases
        lines = existing_rl.splitlines()
        header = lines[0]
        existing_rl = '\n'.join(lines[1:])
    else:
        header += f' ({date})\n\n'

    header += '\n' + minor_header + '\n'

    notes = header
    notes += notes_body
    notes += "\n\n"
    notes += existing_rl

    # make pre-commit happy about one newline
    notes = notes.rstrip()
    notes += "\n"

    with open(rl_path, 'w') as fh:
        fh.write(notes)

    return rl_path
