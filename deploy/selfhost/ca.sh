#!/bin/sh
# ca.sh — the authority behind the server's internal certificates (GTA2_TLS=internal): saves it
# as gta2-ca.pem next to this script, prints its SHA-256 fingerprint and what to do with it.
# Run it once the server is up (docker compose up -d). The authority lasts about 10 years, as
# long as the caddy-data volume is kept.
set -eu
cd "$(dirname "$0")"

tls=$(sed -n 's/^GTA2_TLS=//p' .env 2>/dev/null | tail -n 1)
if [ -n "$tls" ] && [ "$tls" != internal ]; then
  echo "GTA2_TLS=$tls: Let's Encrypt certificates, the clients trust them as they are."
  exit 0
fi

if ! docker compose exec -T caddy cat /data/caddy/pki/authorities/local/root.crt > gta2-ca.pem.new 2>/dev/null \
   || ! grep -q "BEGIN CERTIFICATE" gta2-ca.pem.new; then
  rm -f gta2-ca.pem.new
  echo "No authority yet: is the server up (docker compose up -d)? See: docker compose logs caddy" >&2
  exit 1
fi
mv gta2-ca.pem.new gta2-ca.pem

echo "Saved: $PWD/gta2-ca.pem"
if command -v openssl >/dev/null 2>&1; then
  openssl x509 -in gta2-ca.pem -noout -subject -enddate
  openssl x509 -in gta2-ca.pem -noout -fingerprint -sha256
else
  echo "(openssl not found: no fingerprint shown)"
fi
cat <<'EOF'

Consoles (GTA 2 Online): copy gta2-ca.pem to ports/gta2/conf/, then in conf/online.ini, in the
  server's [server] section:   ca_file = gta2-ca.pem
  (not tls_fingerprint: the server's own certificate changes every 12 hours, the authority not)
Browsers: install gta2-ca.pem as a trusted authority, then restart the browser.
  macOS: open it, Keychain Access, "Always Trust".  Windows: "Trusted Root Certification
  Authorities".  Firefox: Settings, Certificates, Authorities, Import.
  Android / iOS: install it as a CA certificate, then (iOS) enable full trust for it.
Anyone can get it from http://<host>/ca.crt when GTA2_SHARE_CA=yes: compare the fingerprint.
EOF
