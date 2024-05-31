from . import create_release
import argparse
import sys


def do_build(args):
    create_release.build_artifacts(no_check_git=args.no_check_git)


def do_tag(args):
    create_release.do_tag_merge(force_tag=args.force_tag,
                                no_check_git=args.no_check_git)


def do_upload(args):
    create_release.setup_creds()
    create_release.upload_artifacts(force_push_tag=args.force_push_tag,
                                    noconfirm=args.noconfirm)

def do_prepare(args):
    create_release.prepare_release_notes()


def main():
    ap = argparse.ArgumentParser(description='*Lix ur release engineering*')

    def fail(args):
        ap.print_usage()
        sys.exit(1)

    ap.set_defaults(cmd=fail)

    sps = ap.add_subparsers()

    prepare = sps.add_parser('prepare', help='Prepares for a release by moving the release notes over.')
    prepare.set_defaults(cmd=do_prepare)

    tag = sps.add_parser(
        'tag',
        help=
        'Create the tag for the current release in .version and merge it back to the current branch, then switch to it'
    )
    tag.add_argument('--no-check-git',
                     action='store_true',
                     help="Don't check git state before tagging. For testing.")
    tag.add_argument('--force-tag',
                     action='store_true',
                     help='Overwrite the existing tag. For testing.')
    tag.set_defaults(cmd=do_tag)

    build = sps.add_parser(
        'release',
        help=
        'Build an artifacts/ directory with the things that would be released')
    build.add_argument(
        '--no-check-git',
        action='store_true',
        help="Don't check git state before building. For testing.")
    build.set_defaults(cmd=do_build)

    upload = sps.add_parser(
        'upload', help='Upload artifacts to cache and releases bucket')
    upload.add_argument('--force-push-tag',
                        action='store_true',
                        help='Force push the tag. For testing.')
    upload.add_argument(
        '--noconfirm',
        action='store_true',
        help="Don't ask for confirmation. For testing/automation.")
    upload.set_defaults(cmd=do_upload)

    args = ap.parse_args()
    args.cmd(args)
