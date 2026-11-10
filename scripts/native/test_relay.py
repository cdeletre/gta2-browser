#!/usr/bin/env python3
"""A stand-in for server/server.mjs's /relay and login, for gta2-mp's tests in the build
container (its Node is too old for server.mjs): the same relay rules (join, peers, joined,
left, msg / ping / pong to one or everyone else, binary [id length, id, payload]), the same
login (POST /login password=..., or /?invite=<code>, giving the gta2_session cookie), in
plain TCP or in TLS (--cert, --key), to try gta2-mp's wss://. Python 3.8, no packages.

  test_relay.py --port 8443 [--cert server.pem --key server.key] [--password pw]
"""
import argparse
import asyncio
import base64
import hashlib
import json
import ssl
import struct
import sys
import urllib.parse

MAX_ROOM = 12
SESSION = "test-session"
INVITE = "test-invite"

rooms = {}  # room -> {id: Peer}


class Peer:
    def __init__(self, writer):
        self.writer = writer
        self.room = None
        self.id = None

    def send(self, opcode, payload):
        n = len(payload)
        head = bytes([0x80 | opcode])
        if n < 126:
            head += bytes([n])
        elif n < 65536:
            head += bytes([126]) + struct.pack(">H", n)
        else:
            head += bytes([127]) + struct.pack(">Q", n)
        self.writer.write(head + payload)

    def text(self, obj):
        self.send(1, json.dumps(obj, separators=(",", ":")).encode())

    def others(self, to=None):
        members = rooms.get(self.room, {})
        return [p for i, p in members.items() if i != self.id and (not to or i == to)]


def on_message(peer, opcode, data):
    if opcode == 2:
        if not peer.room or len(data) < 1:
            return
        n = data[0]
        to = data[1:1 + n].decode(errors="replace")
        frm = peer.id.encode()
        out = bytes([len(frm)]) + frm + data[1 + n:]
        for p in peer.others(to):
            p.send(2, out)
        return
    try:
        m = json.loads(data.decode())
    except ValueError:
        return
    if not isinstance(m, dict):
        return
    if m.get("t") == "join" and not peer.room and isinstance(m.get("room"), str) and isinstance(m.get("id"), str) \
            and len(m["id"]) < 64:
        members = rooms.get(m["room"][:32], {})
        if len(members) >= MAX_ROOM:
            return peer.text({"t": "full"})
        if m["id"] in members:
            return peer.text({"t": "taken"})
        peer.room = m["room"][:32]
        peer.id = m["id"]
        peer.text({"t": "peers", "ids": list(members.keys())})
        for p in members.values():
            p.text({"t": "joined", "id": peer.id})
        members[peer.id] = peer
        rooms[peer.room] = members
    elif peer.room and m.get("t") in ("msg", "ping", "pong"):
        out = dict(m)
        out["from"] = peer.id
        for p in peer.others(m.get("to")):
            p.text(out)


async def websocket(reader, peer):
    fragments = b""
    first = 0
    while True:
        b0, b1 = await reader.readexactly(2)
        opcode = b0 & 0x0F
        n = b1 & 0x7F
        if n == 126:
            n = struct.unpack(">H", await reader.readexactly(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", await reader.readexactly(8))[0]
        if n > 64 * 1024:
            return
        mask = await reader.readexactly(4) if b1 & 0x80 else b"\0\0\0\0"
        data = bytes(c ^ mask[i & 3] for i, c in enumerate(await reader.readexactly(n)))
        if opcode == 8:
            return
        if opcode == 9:
            peer.send(10, data)
            continue
        if opcode == 10:
            continue
        if opcode in (1, 2):
            first, fragments = opcode, data
        else:
            fragments += data
        if b0 & 0x80:
            on_message(peer, first, fragments)
            await peer.writer.drain()


async def client(reader, writer, password):
    peer = Peer(writer)
    try:
        head = (await reader.readuntil(b"\r\n\r\n")).decode(errors="replace")
        lines = head.split("\r\n")
        method, target = lines[0].split(" ")[:2]
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        url = urllib.parse.urlsplit(target)
        cookie = "gta2_session=" + SESSION + "; Path=/; HttpOnly"
        if method == "POST" and url.path == "/login":
            body = await reader.readexactly(int(headers.get("content-length", "0")))
            ok = urllib.parse.parse_qs(body.decode()).get("password", [""])[0] == password
            writer.write(("HTTP/1.1 303 See Other\r\nLocation: /\r\n" + ("Set-Cookie: " + cookie + "\r\n" if ok else "") +
                          "Content-Length: 0\r\nConnection: close\r\n\r\n").encode())
            return
        if url.path != "/relay":
            ok = urllib.parse.parse_qs(url.query).get("invite", [""])[0] == INVITE
            writer.write(("HTTP/1.1 200 OK\r\n" + ("Set-Cookie: " + cookie + "\r\n" if ok else "") +
                          "Content-Length: 0\r\nConnection: close\r\n\r\n").encode())
            return
        logged = not password or ("gta2_session=" + SESSION) in headers.get("cookie", "")
        if not logged or headers.get("upgrade", "").lower() != "websocket":
            writer.write(b"HTTP/1.1 403 Forbidden\r\n\r\n")
            return
        accept = base64.b64encode(hashlib.sha1((headers["sec-websocket-key"] +
                                                "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        writer.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: " + accept + "\r\n\r\n").encode())
        await writer.drain()
        await websocket(reader, peer)
    except (asyncio.IncompleteReadError, ConnectionError, ssl.SSLError, ValueError, KeyError):
        pass
    finally:
        members = rooms.get(peer.room)
        if members is not None and peer.id in members:
            del members[peer.id]
            if not members:
                del rooms[peer.room]
            for p in members.values():
                p.text({"t": "left", "id": peer.id})
        try:
            await writer.drain()
            writer.close()
        except (ConnectionError, ssl.SSLError):
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8443)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--cert")
    ap.add_argument("--key")
    ap.add_argument("--password", default="")
    a = ap.parse_args()
    ctx = None
    if a.cert:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(a.cert, a.key)
    loop = asyncio.get_event_loop()
    server = loop.run_until_complete(asyncio.start_server(
        lambda r, w: client(r, w, a.password), a.host, a.port, ssl=ctx))
    print("test relay on %s://%s:%d%s" % ("wss" if ctx else "ws", a.host, a.port,
                                          " (password)" if a.password else ""), flush=True)
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        pass
    server.close()


if __name__ == "__main__":
    sys.exit(main())
