#!/usr/bin/env bash
set -euo pipefail

client=$1
fixtures=$2
port=19443

openssl s_server -quiet -accept "$port" \
  -cert "$fixtures/server.pem" -key "$fixtures/server-key.pem" \
  -naccept 4 >/dev/null 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true' EXIT

sleep 0.2
kill -0 "$server_pid"

"$client" "$fixtures/ca.der" localhost "$port" 1800000000 1
"$client" "$fixtures/ca.der" wrong.example "$port" 1800000000 0
"$client" "$fixtures/ca.der" localhost "$port" 1577836800 0
"$client" "$fixtures/ca.der" localhost "$port" 2208988800 0

wait "$server_pid"
trap - EXIT
