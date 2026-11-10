// tls_front.mjs — a TLS front for server/server.mjs in tests: takes TLS connections on one
// port and passes their bytes, decrypted, to the server's plain HTTP port. The page, the login
// and the /relay WebSocket then work in https:// and wss://, as behind nginx, with no install.
// Node's own modules only.
//
//   node tls_front.mjs --cert server.pem --key server.key [--port 8791] [--to 127.0.0.1:8790]
import { readFileSync } from 'node:fs';
import { connect } from 'node:net';
import { createServer } from 'node:tls';

const args = Object.fromEntries(process.argv.slice(2).reduce((out, a, i, all) =>
  (a.startsWith('--') ? [...out, [a.slice(2), all[i + 1]]] : out), []));
const port = Number(args.port || 8791);
const [toHost, toPort] = (args.to || '127.0.0.1:8790').split(':');

const server = createServer({ cert: readFileSync(args.cert), key: readFileSync(args.key) }, (client) => {
  const upstream = connect(Number(toPort), toHost);
  upstream.setNoDelay(true);
  client.setNoDelay(true);
  client.pipe(upstream);
  upstream.pipe(client);
  const end = () => { client.destroy(); upstream.destroy(); };
  client.on('error', end);
  upstream.on('error', end);
  client.on('close', end);
  upstream.on('close', end);
});
server.on('tlsClientError', (e) => console.log(`tls_front: refused a client: ${e.code || e.message}`));
server.listen(port, '0.0.0.0', () => console.log(`tls_front: https/wss on ${port} -> ${toHost}:${toPort}`));
