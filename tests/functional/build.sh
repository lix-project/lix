source common.sh

clearStore

BUILD_DIR=$(mktemp -d)
# ensure that the build directory parent is not world-accessible
chmod 0755 "$BUILD_DIR"
FIFO="$BUILD_DIR/fifo"
mkfifo "$FIFO"
(
  echo > "$FIFO"
  trap 'echo > "$FIFO"' EXIT
  mode=$(stat -c %a $BUILD_DIR/b/*)
  [ "$mode" = "700" -o "$mode" = "710" ]
) &
nix build --build-dir "$BUILD_DIR/b" -E '
  with import ./config.nix; mkDerivation {
    name = "test";
    buildCommand = "cat '"$FIFO"'; cat '"$FIFO"' > $out";
  }' \
  --extra-sandbox-paths "$FIFO" \
  --impure \
  --no-link
wait
