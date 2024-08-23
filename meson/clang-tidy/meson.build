# The clang-tidy target for Lix

run_clang_tidy = find_program('run-clang-tidy', required : false)
# Although this looks like it wants to be pkg-config, pkg-config does not
# really work for *plugins*, which are executable-like .so files that also
# cannot be found via find_program. Fun!
if get_option('lix-clang-tidy-checks-path') != ''
  lix_clang_tidy_so = get_option('lix-clang-tidy-checks-path')
  lix_clang_tidy_so_found = true
else
  lix_clang_tidy_subproj = subproject(
    'lix-clang-tidy',
    required : false,
    default_options : {'build-by-default': false}
  )
  if lix_clang_tidy_subproj.found()
    lix_clang_tidy_so = lix_clang_tidy_subproj.get_variable('lix_clang_tidy')
    lix_clang_tidy_so_found = true
  else
    lix_clang_tidy_so_found = false
  endif
endif

# Due to numerous problems, such as:
# - Meson does not expose pch targets, but *fine*, I can just ask Ninja for
#   them with `ninja -t targets rule cpp_PCH` and build them manually:
#   https://github.com/mesonbuild/meson/issues/13499
# - Nixpkgs stdenv buries the cc-wrapper under a giant pile of assumptions
#   about the cc-wrapper actually being used on the cc of a stdenv, rather than
#   independently for clang-tidy, and we need to use cc-wrapper to get the
#   correct hardening flags so that clang-tidy can actually parse the PCH file
#
# I give up. I am going to delete the damn PCH args and then it will work.
meson.add_postconf_script(
  python,
  meson.current_source_dir() / 'clean_compdb.py',
  meson.global_build_root() / 'compile_commands.json',
  meson.current_build_dir() / 'compile_commands.json',
)

# Horrible hack to get around not being able to depend on another target's
# generated headers in any way in the meson DSL
# https://github.com/mesonbuild/meson/issues/12817 which was incorrectly
# closed, if you *actually* need to generate the files once.
# Also related: https://github.com/mesonbuild/meson/issues/3667
#
# Or we could ban meson generators because their design is broken.
build_all_generated_headers = custom_target(
  command : [
    python,
    meson.current_source_dir() / 'build_required_targets.py',
    meson.global_build_root(),
  ],
  output : 'generated_headers.stamp',
  build_by_default : false,
  build_always_stale : true,
)

if lix_clang_tidy_so_found
  run_clang_tidy_args = [
    meson.current_source_dir() / 'clang-tidy-runner.py',
    '--run-clang-tidy-path', run_clang_tidy,
    '--compdb-path', meson.current_build_dir(),
    '--plugin-path', lix_clang_tidy_so,
  ]
  run_target(
    'clang-tidy',
    command : [
      python,
      run_clang_tidy_args,
      '--werror',
    ],
    depends : [
      build_all_generated_headers,
    ],
  )
  run_target(
    'clang-tidy-fix',
    command : [
      python,
      run_clang_tidy_args,
      '--fix',
    ],
    depends : [
      build_all_generated_headers,
    ],
  )
endif
