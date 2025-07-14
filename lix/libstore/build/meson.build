libstore_rpc += custom_target(
  command : [
    capnpc_wrapper,
    '--language=c++',
    '--src-prefix=@CURRENT_SOURCE_DIR@',
    '--outdir=@OUTDIR@',
    '--depfile=@DEPFILE@',
    '-I@SOURCE_ROOT@',
    '@INPUT@',
  ],
  input : files(
    # keep-sorted start
    'hook-instance.capnp',
    # keep-sorted end
  ),
  output : [
    '@PLAINNAME@.h',
    '@PLAINNAME@.c++',
  ],
  depfile : '@PLAINNAME@.d',
)
