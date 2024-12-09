import argparse
import logging
import sys

from . import create_release, docker, environment
from .environment import RelengEnvironment

log = logging.getLogger(__name__)

def do_build(args):
    if args.target == 'all':
        create_release.build_artifacts(args.profile, no_check_git=args.no_check_git)
    elif args.target == 'manual':
        # n.b. args.profile does nothing here, you will just get the x86_64-linux manual no matter what.
        eval_result = create_release.eval_jobs(args.profile)
        create_release.build_manual(eval_result)
    else:
        raise ValueError('invalid target, unreachable')


def do_tag(args):
    create_release.do_tag_merge(force_tag=args.force_tag,
                                no_check_git=args.no_check_git)

    log.info('Merged the release commit into your last branch, and switched to a detached HEAD of the artifact to be released.')
    log.info('After you are done with releasing, switch to your previous branch and push that branch for review.')


def do_upload(env: RelengEnvironment, args):
    create_release.setup_creds(env)
    if args.target == 'all':
        docker.check_all_logins(env)
        create_release.upload_artifacts(env,
                                        force_push_tag=args.force_push_tag,
                                        noconfirm=args.noconfirm,
                                        no_check_git=args.no_check_git)
    elif args.target == 'manual':
        create_release.upload_manual(env)
    else:
        raise ValueError('invalid target, unreachable')


def do_prepare(args):
    create_release.prepare_release_notes()


def main():
    ap = argparse.ArgumentParser(description='*Lix ur release engineering*')

    def fail(args):
        ap.print_usage()
        sys.exit(1)

    ap.set_defaults(cmd=fail)

    sps = ap.add_subparsers()

    prepare = sps.add_parser(
        'prepare',
        help='Prepare release notes',
        description='Prepares for a release by moving the release notes from `doc/manual/rl-next to doc/manual/src/release-notes/rl-MAJOR.md`.')
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
        'build',
        help=
        'Build an artifacts/ directory with the things that would be released')
    build.add_argument(
        '--no-check-git',
        action='store_true',
        help="Don't check git state before building. For testing.")
    build.add_argument('--target',
                       choices=['manual', 'all'],
                       default='all',
                       help='Whether to build everything or just the manual')
    build.add_argument('--profile',
                       default='all',
                       choices=('all', 'x86_64-linux-only'),
                       help='Which systems to build targets for.')
    build.set_defaults(cmd=do_build)

    upload = sps.add_parser(
        'upload', help='Upload artifacts to cache and releases bucket')
    upload.add_argument(
        '--no-check-git',
        action='store_true',
        help="Don't check git state before uploading. For testing.")
    upload.add_argument('--force-push-tag',
                        action='store_true',
                        help='Force push the tag. For testing.')
    upload.add_argument(
        '--target',
        choices=['manual', 'all'],
        default='all',
        help='Whether to upload a release or just the nightly/otherwise manual'
    )
    upload.add_argument(
        '--noconfirm',
        action='store_true',
        help="Don't ask for confirmation. For testing/automation.")
    upload.add_argument('--environment',
                        choices=list(environment.ENVIRONMENTS.keys()),
                        default='staging',
                        help='Environment to release to')
    upload.set_defaults(cmd=lambda args: do_upload(
        environment.ENVIRONMENTS[args.environment], args))

    args = ap.parse_args()
    args.cmd(args)
