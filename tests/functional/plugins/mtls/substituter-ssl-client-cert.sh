#!/usr/bin/env bash

# shellcheck source=common.sh
source ../../common.sh

# Load the mTLS plugin for these tests.
loadContribPlugin "mtls_store"

# Generate test certificates using EC keys for faster generation

# Generate CA with EC key
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/ca.key" 2>/dev/null
openssl req -new -x509 -days 1 -key "$TEST_ROOT/ca.key" -out "$TEST_ROOT/ca.crt" \
  -subj "/C=US/ST=Test/L=Test/O=TestCA/CN=Test CA" 2>/dev/null

# Generate server certificate with EC key
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/server.key" 2>/dev/null
openssl req -new -key "$TEST_ROOT/server.key" -out "$TEST_ROOT/server.csr" \
  -subj "/C=US/ST=Test/L=Test/O=TestServer/CN=localhost" 2>/dev/null
openssl x509 -req -days 1 -in "$TEST_ROOT/server.csr" -CA "$TEST_ROOT/ca.crt" -CAkey "$TEST_ROOT/ca.key" \
  -set_serial 01 -out "$TEST_ROOT/server.crt" 2>/dev/null

# Generate client certificate with EC key
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/client.key" 2>/dev/null
openssl req -new -key "$TEST_ROOT/client.key" -out "$TEST_ROOT/client.csr" \
  -subj "/C=US/ST=Test/L=Test/O=TestClient/CN=Nix Test Client" 2>/dev/null
openssl x509 -req -days 1 -in "$TEST_ROOT/client.csr" -CA "$TEST_ROOT/ca.crt" -CAkey "$TEST_ROOT/ca.key" \
  -set_serial 02 -out "$TEST_ROOT/client.crt" 2>/dev/null

# Start the server and have it write its chosen port to the FIFO
FIFO_PATH="$TEST_ROOT/server-port.fifo"
mkfifo "$FIFO_PATH"
python3 "$PWD/nix-binary-cache-ssl-server.py" \
  --port-fifo "$FIFO_PATH" \
  --cert "$TEST_ROOT/server.crt" \
  --key "$TEST_ROOT/server.key" \
  --ca-cert "$TEST_ROOT/ca.crt" &
SERVER_PID=$!

# Function to stop server on exit
stopServer() {
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
  rm -f "$FIFO_PATH"
}
trap stopServer EXIT

# Read port from the FIFO (waits until server writes to it) but timeouts after 5s.
if ! PORT=$(timeout 5s bash -c "read -r line < '$FIFO_PATH'; echo \"\$line\""); then
  echo "Timed out waiting for server to write port to FIFO" >&2
  exit 1
fi

if ! curl -sSf -k --cert "$TEST_ROOT/client.crt" --key "$TEST_ROOT/client.key" \
  "https://localhost:$PORT/nix-cache-info" > /dev/null; then
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Server started but did not respond to curl" >&2
  else
    echo "Server failed to start" >&2
  fi
  exit 1
fi

# Test 1: Verify server rejects connections without client certificate
echo "Testing connection without client certificate (should fail)..." >&2
if curl -s -k "https://localhost:$PORT/nix-cache-info" 2>&1 | grep -q "certificate required"; then
  echo "FAIL: Server should have rejected connection" >&2
  exit 1
fi

# Test 2: Verify server accepts connections with client certificate
echo "Testing connection with client certificate..." >&2
RESPONSE=$(curl -v -s -k --cert "$TEST_ROOT/client.crt" --key "$TEST_ROOT/client.key" \
  "https://localhost:$PORT/nix-cache-info")

if ! echo "$RESPONSE" | grepQuiet "StoreDir: "; then
  echo "FAIL: Server should have accepted client certificate: $RESPONSE" >&2
  exit 1
fi

# Test 3: Test Nix with SSL client certificate parameters
# Set up substituter URL with SSL parameters
sslCache="https+mtls://localhost:$PORT?tls-certificate=$TEST_ROOT/client.crt&tls-private-key=$TEST_ROOT/client.key"

# Configure Nix to trust our CA
export NIX_SSL_CERT_FILE="$TEST_ROOT/ca.crt"

# Test nix store info
nix store ping --store "$sslCache" --json # | jq -e '.url' | grepQuiet "https://localhost:$PORT"

# Test 4: Verify incorrect client certificate is rejected
# Generate a different client cert not signed by our CA (also using EC)
openssl ecparam -genkey -name prime256v1 -out "$TEST_ROOT/wrong.key" 2>/dev/null
openssl req -new -x509 -days 1 -key "$TEST_ROOT/wrong.key" -out "$TEST_ROOT/wrong.crt" \
  -subj "/C=US/ST=Test/L=Test/O=Wrong/CN=Wrong Client" 2>/dev/null

wrongCache="https+mtls://localhost:$PORT?tls-certificate=$TEST_ROOT/wrong.crt&tls-private-key=$TEST_ROOT/wrong.key"

rm -rf "$TEST_HOME"

# This should fail
if nix store ping --download-attempts 0 --store "$wrongCache"; then
  echo "FAIL: Should have rejected wrong certificate" >&2
  exit 1
fi
