project('aws-sdk', 'cpp')

# dependency() uses Meson's internal logic and generally relies on pkg-config or CMake,
# but Meson can also use the project's compiler to find a library in the compiler's search
# path. Not in the dependency() function, though. You have to use compiler.find_library(),
# and Meson doesn't have a way to tell dependency() to simply fallback to find_library().
# It *does* however allow dependency() to fallback to a variable defined in a subproject,
# Hence: this file.

# For some reason, these specific components of the AWS C++ SDK aren't found by CMake when
# compiling statically, and `pkgsStatic.aws-sdk-cpp` doesn't even have a pkg-config at all.

cxx = meson.get_compiler('cpp')
aws_cpp_sdk_transfer_dep = cxx.find_library('aws-cpp-sdk-transfer')
aws_cpp_sdk_s3_dep = cxx.find_library('aws-cpp-sdk-s3')
