#!/usr/bin/env bash
set -euo pipefail

client=$1
fixtures=$2
port=19443

umask 077
generated=$(mktemp -d "$fixtures/generated.XXXXXX")
server_pid=

run_openssl() {
  if ! openssl "$@" >"$generated/openssl.log" 2>&1; then
    sed -n '1,200p' "$generated/openssl.log" >&2
    return 1
  fi
}

cleanup() {
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf -- "${generated:?}"
}
trap cleanup EXIT

run_openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 1 \
  -subj "/CN=JaszczurHAL Native Test CA" \
  -addext "basicConstraints=critical,CA:TRUE" \
  -addext "keyUsage=critical,keyCertSign,cRLSign" \
  -keyout "$generated/ca-key.pem" \
  -out "$generated/ca.pem"

run_openssl req -new -newkey rsa:2048 -nodes -sha256 \
  -subj "/CN=localhost" \
  -keyout "$generated/server-key.pem" \
  -out "$generated/server.csr"

run_openssl x509 -req -sha256 -days 1 \
  -in "$generated/server.csr" \
  -CA "$generated/ca.pem" \
  -CAkey "$generated/ca-key.pem" \
  -set_serial 1 \
  -extfile <(
    printf '%s\n' \
      "[server]" \
      "basicConstraints=critical,CA:FALSE" \
      "keyUsage=critical,digitalSignature,keyEncipherment" \
      "extendedKeyUsage=serverAuth" \
      "subjectAltName=DNS:localhost,IP:127.0.0.1"
  ) \
  -extensions server \
  -out "$generated/server.pem"

run_openssl x509 -in "$generated/ca.pem" -outform DER \
  -out "$generated/ca.der"

valid_time=$(date +%s)
before_validity=$((valid_time - 86400))
after_validity=$((valid_time + 172800))

openssl s_server -quiet -accept "127.0.0.1:$port" \
  -cert "$generated/server.pem" -key "$generated/server-key.pem" \
  -naccept 4 >"$generated/server.log" 2>&1 &
server_pid=$!

sleep 0.2
if ! kill -0 "$server_pid" 2>/dev/null; then
  sed -n '1,200p' "$generated/server.log" >&2
  wait "$server_pid" 2>/dev/null || true
  server_pid=
  exit 1
fi

"$client" "$generated/ca.der" localhost "$port" "$valid_time" 1
"$client" "$generated/ca.der" wrong.example "$port" "$valid_time" 0
"$client" "$generated/ca.der" localhost "$port" "$before_validity" 0
"$client" "$generated/ca.der" localhost "$port" "$after_validity" 0

wait "$server_pid"
server_pid=
