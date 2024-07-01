{ runCommandCC }:
runCommandCC "io_uring-is-blocked" { } ''
  cat > test.c <<EOF
  #include <errno.h>
  #include <sys/syscall.h>
  #include <unistd.h>

  int main() {
      int res = syscall(SYS_io_uring_setup, 0, NULL);
      return res == -1 && errno == ENOSYS ? 0 : 1;
  }
  EOF
  "$CC" -o test test.c
  if ! ./test; then
    echo "Oh no! io_uring is available!"
    exit 1
  fi
  touch "$out"
''
