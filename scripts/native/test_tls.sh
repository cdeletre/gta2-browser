#!/usr/bin/env bash
# test_tls.sh — gta2-online's ws:// and wss:// against scripts/native/test_relay.py, in the build
# container: test certificates (an authority, a server certificate for localhost and 127.0.0.1,
# one for another name, an expired one, one not valid yet, a self-signed one, another authority),
# one relay per case, then gta2-net-test's TLS checks (GTA2_NET_TEST_TLS).
#
#   scripts/native/test_tls.sh [build dir, default build/native/debug]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ ! -f /.dockerenv && -z "${GTA2_IN_CONTAINER:-}" ]]; then
  exec docker exec -i -e GTA2_IN_CONTAINER=1 -w /workspace "${GTA2_CONTAINER:-gta2-pm}" \
       ./scripts/native/test_tls.sh "$@"
fi
BUILD="${1:-build/native/debug}"
BASE=18440
T="$(mktemp -d)"
PIDS=()
cleanup() {
  for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done
  rm -rf "$T"
}
trap cleanup EXIT

cd "$T"
q() { "$@" >/dev/null 2>&1; }
# authorities
q openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 30 -subj "/CN=gta2 test CA"
q openssl req -x509 -newkey rsa:2048 -nodes -keyout other-ca.key -out other-ca.pem -days 30 -subj "/CN=another CA"
# certificates signed by the authority, with given names and dates (openssl ca sets the dates)
mkdir -p ca/new && touch ca/index.txt && echo 1000 > ca/serial
cat > ca.cnf <<'EOF'
[ca]
default_ca = test
[test]
dir = ca
database = ca/index.txt
new_certs_dir = ca/new
serial = ca/serial
default_md = sha256
policy = any
unique_subject = no
copy_extensions = copy
[any]
commonName = supplied
EOF
sign() { # name, subjectAltName, start, end
  q openssl req -newkey rsa:2048 -nodes -keyout "$1.key" -out "$1.csr" -subj "/CN=$1" -addext "subjectAltName=$2"
  q openssl ca -batch -config ca.cnf -cert ca.pem -keyfile ca.key -in "$1.csr" -out "$1.pem" \
    -startdate "$3" -enddate "$4" -notext
}
sign good "DNS:localhost,IP:127.0.0.1" 20250101000000Z 20400101000000Z
sign other-name "DNS:other.example" 20250101000000Z 20400101000000Z
sign expired "DNS:localhost" 20200101000000Z 20210101000000Z
sign future "DNS:localhost" 20380101000000Z 20400101000000Z
q openssl req -x509 -newkey rsa:2048 -nodes -keyout self.key -out self.pem -days 30 -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost"
openssl x509 -in self.pem -outform der | sha256sum | cut -d' ' -f1 > self.sha256

relay() { # port, extra args
  python3 "$ROOT/scripts/native/test_relay.py" --port "$1" --password pw "${@:2}" > "relay-$1.log" 2>&1 &
  PIDS+=($!)
}
relay $((BASE + 0))
relay $((BASE + 1)) --cert good.pem --key good.key
relay $((BASE + 2)) --cert other-name.pem --key other-name.key
relay $((BASE + 3)) --cert expired.pem --key expired.key
relay $((BASE + 4)) --cert future.pem --key future.key
relay $((BASE + 5)) --cert self.pem --key self.key
# a relay with no password
python3 "$ROOT/scripts/native/test_relay.py" --port $((BASE + 7)) > "relay-$((BASE + 7)).log" 2>&1 &
PIDS+=($!)
# a server that takes the connection and never answers (handshake timeout)
python3 -c "import socket,time; s=socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('127.0.0.1', $((BASE + 6)))); s.listen(8); time.sleep(600)" &
PIDS+=($!)
for _ in $(seq 50); do
  ready=1
  for i in 0 1 2 3 4 5 6 7; do (echo > /dev/tcp/127.0.0.1/$((BASE + i))) 2>/dev/null || ready=0; done
  [[ $ready -eq 1 ]] && break
  sleep 0.1
done

cd "$ROOT"
GTA2_NET_TEST_TLS="$T:$BASE" "$BUILD/gta2-net-test"
