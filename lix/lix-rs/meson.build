lix_rs_files += files(
  'Cargo.toml',
  # keep-sorted start
  'src/fetchers/git.rs',
  'src/fetchers/mod.rs',
  'src/ffi_test.rs',
  'src/lib.rs',
  'src/machines.rs',
  'src/repl.rs',
  # keep-sorted end
)

lixrs_zngur_bridge = custom_target(
  command : [
    licxxbridge,
    'generate',
    '--in=@INPUT@',
    '--out-header=@OUTPUT0@',
    '--out-shared-header=@OUTPUT1@',
    '--out-impl=@OUTPUT2@',
    '--out-rs=@OUTPUT3@',
    '--out-deps=@DEPFILE@',
  ],
  input : files('main.zng'),
  output : [ '@BASENAME@.gen.hh', 'zngur.gen.hh', '@BASENAME@.gen.cc', '@PLAINNAME@.rs' ],
  depfile : '@PLAINNAME@.d',
  install : true,
  install_dir : [ includedir / 'lix/lix-rs', includedir / 'lix/lix-rs', false, false ],
)

liblix_generated_headers += [ lixrs_zngur_bridge[0], lixrs_zngur_bridge[1] ]
liblix_sources += lixrs_zngur_bridge[2]
lix_rs_files += lixrs_zngur_bridge[3]

liblix_sources += files('utils.cc')

install_headers(
  files('utils.hh'),
  subdir : 'lix/lix-rs',
  preserve_path : true,
)
