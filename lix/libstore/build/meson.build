build_capnp_files = files(
  # keep-sorted start
  'hook-instance.capnp',
  'request.capnp',
  # keep-sorted end
)

foreach infile : build_capnp_files
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
    input : infile,
    output : [
      '@PLAINNAME@.h',
      '@PLAINNAME@.c++',
    ],
    depfile : '@PLAINNAME@.d',
  )
endforeach
