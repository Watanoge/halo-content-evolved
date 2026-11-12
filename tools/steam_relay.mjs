#!/usr/bin/env node
/*
STEAM_RELAY.MJS

A relay on this computer for the Steam Workshop test page
(port/web/site/testing/workshop). Steam's connection servers refuse a
WebSocket from a page that is not Steam's (HTTP 403 on its Origin header), and
its content servers send no cross-origin headers, so a page cannot reach
either; this passes the page's bytes on, unread:

- /cm (a WebSocket): to one of Steam's connection servers, each message as it
  is, both ways;
- /cdn/depot/...: a depot's manifest or chunk from one of Steam's content
  servers (the chunks are encrypted; the page has the key, this has not);
- /testing/workshop/: the test page itself, so that the page and the relay
  are one origin.

The Steam session's token is in the messages this passes on. That is why it
listens on this computer only (127.0.0.1) and takes a WebSocket only from a
page of this computer (or of an origin named with --origin): run it yourself;
do not put it on a server.

  node tools/steam_relay.mjs [--port 8791] [--origin https://example.com]

then open http://localhost:8791/testing/workshop/. Needs nothing but Node.
*/

import http from 'node:http';
import https from 'node:https';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';
const MAX_MESSAGE = 64 * 1024 * 1024;

// one end of a WebSocket over a socket whose handshake is done: whole
// messages out (binary), whole messages in (onmessage), pings answered
export class WsConn {
  constructor(socket, masks) {
    this.socket = socket;
    this.masks = masks; // a client masks what it sends
    this.buffer = Buffer.alloc(0);
    this.parts = [];
    this.partsSize = 0;
    this.closed = false;
    this.onmessage = () => {};
    this.onclose = () => {};
    socket.on('data', (data) => this.receive(data));
    socket.on('close', () => this.finish());
    socket.on('error', () => this.finish());
  }

  finish() {
    if (this.closed) return;
    this.closed = true;
    this.socket.destroy();
    this.onclose();
  }

  frame(opcode, payload) {
    const length = payload.length;
    let header;
    if (length < 126) {
      header = Buffer.from([0x80 | opcode, length]);
    } else if (length < 65536) {
      header = Buffer.alloc(4);
      header[0] = 0x80 | opcode;
      header[1] = 126;
      header.writeUInt16BE(length, 2);
    } else {
      header = Buffer.alloc(10);
      header[0] = 0x80 | opcode;
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(length), 2);
    }
    if (!this.masks) return Buffer.concat([header, payload]);
    header[1] |= 0x80;
    const mask = crypto.randomBytes(4);
    const masked = Buffer.from(payload);
    for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i & 3];
    return Buffer.concat([header, mask, masked]);
  }

  send(payload) {
    if (!this.closed) this.socket.write(this.frame(2, Buffer.from(payload)));
  }

  close() {
    if (this.closed) return;
    try {
      this.socket.end(this.frame(8, Buffer.alloc(0)));
    } catch {
      // (the socket went first)
    }
    this.finish();
  }

  receive(data) {
    this.buffer = this.buffer.length ? Buffer.concat([this.buffer, data]) : data;
    for (;;) {
      const buffer = this.buffer;
      if (buffer.length < 2) return;
      const masked = (buffer[1] & 0x80) !== 0;
      let length = buffer[1] & 0x7f;
      let at = 2;
      if (length === 126) {
        if (buffer.length < 4) return;
        length = buffer.readUInt16BE(2);
        at = 4;
      } else if (length === 127) {
        if (buffer.length < 10) return;
        const long = buffer.readBigUInt64BE(2);
        if (long > BigInt(MAX_MESSAGE)) return this.close();
        length = Number(long);
        at = 10;
      }
      const start = at + (masked ? 4 : 0);
      if (buffer.length < start + length) return;
      let payload = buffer.subarray(start, start + length);
      if (masked) {
        const mask = buffer.subarray(at, at + 4);
        payload = Buffer.from(payload);
        for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      }
      this.buffer = buffer.subarray(start + length);
      const opcode = buffer[0] & 0x0f;
      const final = (buffer[0] & 0x80) !== 0;
      if (opcode === 8) return this.close();
      if (opcode === 9) {
        this.socket.write(this.frame(10, payload));
      } else if (opcode === 0 || opcode === 1 || opcode === 2) {
        this.parts.push(payload);
        this.partsSize += payload.length;
        if (this.partsSize > MAX_MESSAGE) return this.close();
        if (final) {
          const message = this.parts.length === 1 ? Buffer.from(this.parts[0]) : Buffer.concat(this.parts);
          this.parts = [];
          this.partsSize = 0;
          this.onmessage(message);
        }
      }
    }
  }
}

// a WebSocket to a wss address, as its client (no Origin header: Steam takes
// that, as it takes its own client's)
export function wsConnect(address) {
  return new Promise((resolve, reject) => {
    const url = new URL(address);
    const secure = url.protocol === 'wss:';
    const key = crypto.randomBytes(16).toString('base64');
    const request = (secure ? https : http).request({
      host: url.hostname,
      port: url.port || (secure ? 443 : 80),
      path: url.pathname + url.search,
      headers: { Connection: 'Upgrade', Upgrade: 'websocket', 'Sec-WebSocket-Version': '13', 'Sec-WebSocket-Key': key },
    });
    request.on('upgrade', (response, socket, head) => {
      const accept = crypto.createHash('sha1').update(key + WS_GUID).digest('base64');
      if (response.headers['sec-websocket-accept'] !== accept) {
        socket.destroy();
        return reject(new Error('not a WebSocket answer'));
      }
      const conn = new WsConn(socket, true);
      if (head.length) conn.receive(head);
      resolve(conn);
    });
    request.on('response', (response) => reject(new Error('HTTP ' + response.statusCode)));
    request.on('error', reject);
    request.setTimeout(15000, () => request.destroy(new Error('timed out')));
    request.end();
  });
}

async function steamApi(name) {
  const response = await fetch('https://api.steampowered.com/' + name);
  if (!response.ok) throw new Error(name + ': HTTP ' + response.status);
  return (await response.json()).response;
}

// Steam's lists, asked once and again after ten minutes
const lists = { cm: null, cdn: null };
async function listed(kind) {
  const held = lists[kind];
  if (held && Date.now() - held.time < 600000) return held.hosts;
  let hosts;
  if (kind === 'cm') {
    const response = await steamApi('ISteamDirectory/GetCMListForConnect/v1/?cellid=0&format=json&cmtype=websockets');
    hosts = response.serverlist.filter((server) => server.type === 'websockets' && server.realm === 'steamglobal')
      .map((server) => server.endpoint);
  } else {
    const response = await steamApi('IContentServerDirectoryService/GetServersForSteamPipe/v1/?cell_id=0');
    hosts = response.servers.filter((server) => (server.type === 'SteamCache' || server.type === 'CDN') &&
      server.https_support !== 'unavailable' && !server.steam_china_only && !(server.allowed_app_ids || []).length)
      .map((server) => server.vhost || server.host);
  }
  hosts = hosts.filter((host) => /^[a-z0-9.-]+(:\d+)?$/i.test(host));
  if (!hosts.length) throw new Error('Steam named no ' + kind + ' server');
  lists[kind] = { time: Date.now(), hosts };
  return hosts;
}

const CDN_PATH = /^\/depot\/\d{1,10}\/(chunk\/[0-9a-f]{40}|manifest\/\d{1,20}\/5(\/\d{1,20})?)$/;

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8' };

export function startRelay({ port = 8791, origins = [], quiet = false } = {}) {
  const pageDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../port/web/site/testing/workshop');
  const log = (...words) => { if (!quiet) console.log(...words); };

  // a page of this computer, or of an origin named when the relay started
  function allowed(origin) {
    if (!origin) return true; // (not a browser)
    if (origins.includes(origin)) return true;
    try {
      const host = new URL(origin).hostname;
      return host === 'localhost' || host === '127.0.0.1' || host === '[::1]';
    } catch {
      return false;
    }
  }

  function cors(request, headers = {}) {
    const origin = request.headers.origin;
    if (origin && allowed(origin)) {
      headers['Access-Control-Allow-Origin'] = origin;
      headers.Vary = 'Origin';
      headers['Access-Control-Expose-Headers'] = 'Content-Length';
    }
    headers['Cross-Origin-Resource-Policy'] = 'cross-origin';
    return headers;
  }

  async function cdn(request, response, pathname) {
    const hosts = await listed('cdn');
    let last = 'no server';
    for (let attempt = 0; attempt < 4; attempt++) {
      const host = hosts[Math.floor(Math.random() * hosts.length)];
      try {
        const upstream = await fetch('https://' + host + pathname, { signal: AbortSignal.timeout(60000) });
        if (upstream.status === 404 || upstream.status === 401 || upstream.status === 403) {
          response.writeHead(upstream.status, cors(request));
          return response.end();
        }
        if (!upstream.ok) {
          last = 'HTTP ' + upstream.status;
          continue;
        }
        const bytes = Buffer.from(await upstream.arrayBuffer());
        response.writeHead(200, cors(request, {
          'Content-Type': 'application/octet-stream', 'Content-Length': bytes.length, 'Cache-Control': 'no-store' }));
        return response.end(bytes);
      } catch (error) {
        last = error.message;
      }
    }
    response.writeHead(502, cors(request, { 'Content-Type': 'text/plain' }));
    response.end('Steam content server: ' + last);
  }

  const server = http.createServer((request, response) => {
    const url = new URL(request.url, 'http://localhost');
    if (!allowed(request.headers.origin)) {
      response.writeHead(403);
      return response.end();
    }
    if (request.method === 'OPTIONS') {
      response.writeHead(204, cors(request, { 'Access-Control-Allow-Methods': 'GET' }));
      return response.end();
    }
    if (request.method !== 'GET') {
      response.writeHead(405);
      return response.end();
    }
    if (url.pathname === '/relay') {
      response.writeHead(200, cors(request, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }));
      return response.end(JSON.stringify({ relay: 'steam', version: 1 }));
    }
    if (url.pathname.startsWith('/cdn/')) {
      const pathname = url.pathname.slice(4);
      if (!CDN_PATH.test(pathname)) {
        response.writeHead(400, cors(request));
        return response.end();
      }
      return cdn(request, response, pathname).catch((error) => {
        if (!response.headersSent) response.writeHead(502, cors(request, { 'Content-Type': 'text/plain' }));
        response.end(String(error.message));
      });
    }
    if (url.pathname === '/' || url.pathname === '/testing/workshop') {
      response.writeHead(302, { Location: '/testing/workshop/' });
      return response.end();
    }
    if (url.pathname.startsWith('/testing/workshop/')) {
      const name = url.pathname.slice('/testing/workshop/'.length) || 'index.html';
      if (!/^[a-z0-9_-]+\.(html|js|css)$/.test(name)) {
        response.writeHead(404);
        return response.end();
      }
      return fs.readFile(path.join(pageDir, name), (error, bytes) => {
        if (error) {
          response.writeHead(404);
          return response.end();
        }
        response.writeHead(200, { 'Content-Type': TYPES[path.extname(name)], 'Cache-Control': 'no-store' });
        response.end(bytes);
      });
    }
    response.writeHead(404);
    response.end();
  });

  server.on('upgrade', async (request, socket) => {
    const refuse = (status) => socket.end('HTTP/1.1 ' + status + '\r\nConnection: close\r\n\r\n');
    const key = request.headers['sec-websocket-key'];
    if (new URL(request.url, 'http://localhost').pathname !== '/cm' || !key) return refuse('404 Not Found');
    // a browser always names a page's origin here; another site's page is refused
    if (!allowed(request.headers.origin)) return refuse('403 Forbidden');
    socket.on('error', () => {});
    let steam = null;
    try {
      const hosts = await listed('cm');
      for (let attempt = 0; attempt < 3 && !steam; attempt++) {
        const host = hosts[Math.floor(Math.random() * Math.min(hosts.length, 8))];
        try {
          steam = await wsConnect('wss://' + host + '/cmsocket/');
          log('session: connected to', host);
        } catch (error) {
          log('session:', host, error.message);
        }
      }
    } catch (error) {
      log('session:', error.message);
    }
    if (!steam) return refuse('502 Bad Gateway');
    const accept = crypto.createHash('sha1').update(key + WS_GUID).digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
      'Sec-WebSocket-Accept: ' + accept + '\r\n\r\n');
    const page = new WsConn(socket, false);
    page.onmessage = (message) => steam.send(message);
    steam.onmessage = (message) => page.send(message);
    page.onclose = () => { steam.close(); log('session: closed'); };
    steam.onclose = () => page.close();
  });

  return new Promise((resolve, reject) => {
    server.on('error', reject);
    server.listen(port, '127.0.0.1', () => resolve(server));
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const args = process.argv.slice(2);
  let port = 8791;
  const origins = [];
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--port') port = Number(args[++i]);
    else if (args[i] === '--origin') origins.push(args[++i]);
    else {
      console.error('usage: node tools/steam_relay.mjs [--port 8791] [--origin https://example.com]');
      process.exit(2);
    }
  }
  startRelay({ port, origins }).then(() => {
    console.log('Steam relay for the Workshop test page, on this computer only.');
    console.log('Open http://localhost:' + port + '/testing/workshop/');
  }, (error) => {
    console.error(error.message);
    process.exit(1);
  });
}
