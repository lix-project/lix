licxxbridge = get_option('licxxbridge')

if licxxbridge == ''
  licxxbridge = custom_target(
    'licxxbridge',
    input : files(
      '../../Cargo.toml',
      '../../Cargo.lock',
      'Cargo.toml',
      'main.rs',
    ),
    output : 'licxxbridge',
    command : [
      cargo,
      '-Z', 'unstable-options',
      'build',
      '--artifact-dir=@0@'.format(meson.current_build_dir()),
      '-p',
      'licxxbridge',
      cargo_jobs_args,
    ],
    console : true,
  )
endif
