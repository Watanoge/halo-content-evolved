/*
STEAM.JS

What the Workshop test page needs of Steam's client protocol, in the page:

- the session with a connection server (SteamSession): sign-in by a QR code
  the Steam mobile app approves, the account's licences, product information
  (which depots an app has), depot keys, manifest request codes, and Workshop
  queries;
- depot content (Depot): a depot's manifest (its files and their chunks) and
  the chunks, decrypted with the depot's key and unpacked here.

The page cannot reach Steam itself (Steam refuses other sites' pages), so
both go through a relay (tools/steam_relay.mjs) that passes the bytes on. The
token and the depot keys stay in this page's memory.

The message layouts are Steam's (as SteamKit and DepotDownloader document
them); only the fields used here are described.
*/

import { lzmaDecode } from './lzma.js';
import { decompress as zstdDecode } from './fzstd.js';

// ---------- protocol buffers: a schema is {name: [number, type, repeated]},
// a type a scalar's name or a schema; 64-bit values are BigInts

const encoder = new TextEncoder();
const decoder = new TextDecoder();

class Writer {
  constructor() {
    this.bytes = [];
  }

  varint(value) {
    let v = BigInt.asUintN(64, BigInt(value));
    while (v > 127n) {
      this.bytes.push(Number(v & 127n) | 128);
      v >>= 7n;
    }
    this.bytes.push(Number(v));
  }

  fixed(value, size) {
    let v = BigInt.asUintN(64, BigInt(value));
    for (let i = 0; i < size; i++) {
      this.bytes.push(Number(v & 255n));
      v >>= 8n;
    }
  }

  raw(bytes) {
    for (const byte of bytes) this.bytes.push(byte);
  }
}

const WIRE = { uint32: 0, int32: 0, uint64: 0, bool: 0, fixed64: 1, fixed32: 5, float: 5, string: 2, bytes: 2 };

export function encode(schema, message) {
  const writer = new Writer();
  for (const [name, [number, type, repeated]] of Object.entries(schema)) {
    const value = message[name];
    if (value === undefined || value === null) continue;
    for (const item of repeated ? value : [value]) {
      const wire = typeof type === 'string' ? WIRE[type] : 2;
      writer.varint((number << 3) | wire);
      if (typeof type !== 'string') {
        const inner = encode(type, item);
        writer.varint(inner.length);
        writer.raw(inner);
      } else if (type === 'string' || type === 'bytes') {
        const bytes = type === 'string' ? encoder.encode(item) : item;
        writer.varint(bytes.length);
        writer.raw(bytes);
      } else if (type === 'fixed64') {
        writer.fixed(item, 8);
      } else if (type === 'fixed32') {
        writer.fixed(item, 4);
      } else if (type === 'float') {
        writer.raw(new Uint8Array(new Float32Array([item]).buffer));
      } else if (type === 'bool') {
        writer.varint(item ? 1 : 0);
      } else {
        writer.varint(item);
      }
    }
  }
  return new Uint8Array(writer.bytes);
}

function readVarint(bytes, state) {
  let value = 0n;
  let shift = 0n;
  for (;;) {
    if (state.at >= bytes.length || shift > 63n) throw new Error('a message is cut short');
    const byte = bytes[state.at++];
    value |= BigInt(byte & 127) << shift;
    if (!(byte & 128)) return value;
    shift += 7n;
  }
}

function scalar(type, value) {
  switch (type) {
    case 'uint32': return Number(BigInt.asUintN(32, value));
    case 'int32': return Number(BigInt.asIntN(32, value));
    case 'bool': return value !== 0n;
    default: return value; // uint64, fixed64
  }
}

export function decode(schema, bytes) {
  const fields = new Map();
  for (const [name, [number, type, repeated]] of Object.entries(schema)) fields.set(number, { name, type, repeated });
  const message = {};
  for (const { name, repeated } of fields.values()) if (repeated) message[name] = [];
  const state = { at: 0 };
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  while (state.at < bytes.length) {
    const tag = Number(readVarint(bytes, state));
    const wire = tag & 7;
    const field = fields.get(tag >>> 3);
    let value;
    if (wire === 0) {
      value = readVarint(bytes, state);
      if (field) value = scalar(field.type, value);
    } else if (wire === 1) {
      if (state.at + 8 > bytes.length) throw new Error('a message is cut short');
      value = view.getBigUint64(state.at, true);
      state.at += 8;
    } else if (wire === 5) {
      if (state.at + 4 > bytes.length) throw new Error('a message is cut short');
      value = field && field.type === 'float' ? view.getFloat32(state.at, true) : view.getUint32(state.at, true);
      state.at += 4;
    } else if (wire === 2) {
      const length = Number(readVarint(bytes, state));
      if (state.at + length > bytes.length) throw new Error('a message is cut short');
      const inner = bytes.subarray(state.at, state.at + length);
      state.at += length;
      if (!field) continue;
      if (typeof field.type !== 'string') {
        value = decode(field.type, inner);
      } else if (field.type === 'string') {
        value = decoder.decode(inner);
      } else if (field.type === 'bytes') {
        value = inner;
      } else {
        // packed scalars
        const packed = { at: 0 };
        while (packed.at < inner.length) message[field.name].push(scalar(field.type, readVarint(inner, packed)));
        continue;
      }
    } else {
      throw new Error('a message has a field of an unknown kind');
    }
    if (!field) continue;
    if (field.repeated) message[field.name].push(value);
    else message[field.name] = value;
  }
  return message;
}

// ---------- Steam's messages

const EMSG = {
  Multi: 1, ServiceMethod: 146, ServiceMethodResponse: 147, ServiceMethodCallFromClient: 151,
  ClientHeartBeat: 703, ClientLogOnResponse: 751, ClientLoggedOff: 757, ClientLicenseList: 780,
  ClientGetDepotDecryptionKey: 5438, ClientGetDepotDecryptionKeyResponse: 5439, ClientLogon: 5514,
  ClientPICSProductInfoRequest: 8903, ClientPICSProductInfoResponse: 8904,
  ClientPICSAccessTokenRequest: 8905, ClientPICSAccessTokenResponse: 8906,
  ServiceMethodCallFromClientNonAuthed: 9804, ClientHello: 9805,
};
const PROTO_MASK = 0x80000000;
const PROTOCOL_VERSION = 65580;

const Header = {
  steamid: [1, 'fixed64'], client_sessionid: [2, 'int32'], jobid_source: [10, 'fixed64'],
  jobid_target: [11, 'fixed64'], target_job_name: [12, 'string'], eresult: [13, 'int32'],
  error_message: [14, 'string'],
};
const Multi = { size_unzipped: [1, 'uint32'], message_body: [2, 'bytes'] };
const Hello = { protocol_version: [1, 'uint32'] };
const Logon = {
  protocol_version: [1, 'uint32'], cell_id: [3, 'uint32'], client_language: [6, 'string'],
  client_os_type: [7, 'uint32'], machine_name: [96, 'string'], supports_rate_limit_response: [102, 'bool'],
  access_token: [108, 'string'],
};
const LogonResponse = { eresult: [1, 'int32'], heartbeat_seconds: [2, 'int32'], cell_id: [7, 'uint32'] };
const LoggedOff = { eresult: [1, 'int32'] };
const LicenseList = {
  eresult: [1, 'int32'],
  licenses: [2, { package_id: [1, 'uint32'], flags: [7, 'uint32'], access_token: [17, 'uint64'] }, true],
};
const ProductToken = { id: [1, 'uint32'], access_token: [2, 'uint64'] };
const ProductInfoRequest = { packages: [1, ProductToken, true], apps: [2, ProductToken, true], meta_data_only: [3, 'bool'] };
const ProductInfo = { id: [1, 'uint32'], missing_token: [3, 'bool'], buffer: [5, 'bytes'] };
const ProductInfoResponse = {
  apps: [1, ProductInfo, true], unknown_appids: [2, 'uint32', true], packages: [3, ProductInfo, true],
  unknown_packageids: [4, 'uint32', true], response_pending: [6, 'bool'],
};
const AccessTokenRequest = { packageids: [1, 'uint32', true], appids: [2, 'uint32', true] };
const AccessTokenResponse = { app_access_tokens: [3, ProductToken, true], app_denied_tokens: [4, 'uint32', true] };
const DepotKeyRequest = { depot_id: [1, 'uint32'], app_id: [2, 'uint32'] };
const DepotKeyResponse = { eresult: [1, 'int32'], depot_id: [2, 'uint32'], depot_encryption_key: [3, 'bytes'] };

const DeviceDetails = {
  device_friendly_name: [1, 'string'], platform_type: [2, 'int32'], os_type: [3, 'int32'], gaming_device_type: [4, 'uint32'],
};
const BeginQrRequest = {
  device_friendly_name: [1, 'string'], platform_type: [2, 'int32'], device_details: [3, DeviceDetails], website_id: [4, 'string'],
};
const BeginQrResponse = {
  client_id: [1, 'uint64'], challenge_url: [2, 'string'], request_id: [3, 'bytes'], interval: [4, 'float'],
};
const PollRequest = { client_id: [1, 'uint64'], request_id: [2, 'bytes'] };
const PollResponse = {
  new_client_id: [1, 'uint64'], new_challenge_url: [2, 'string'], refresh_token: [3, 'string'],
  access_token: [4, 'string'], had_remote_interaction: [5, 'bool'], account_name: [6, 'string'],
};
const RequestCodeRequest = { app_id: [1, 'uint32'], depot_id: [2, 'uint32'], manifest_id: [3, 'uint64'], app_branch: [4, 'string'] };
const RequestCodeResponse = { manifest_request_code: [1, 'uint64'] };

const FileTag = { tag: [1, 'string'], display_name: [3, 'string'] };
export const FileDetails = {
  result: [1, 'uint32'], publishedfileid: [2, 'uint64'], creator: [3, 'fixed64'], consumer_appid: [5, 'uint32'],
  file_size: [8, 'uint64'], preview_url: [11, 'string'], hcontent_file: [14, 'fixed64'], title: [16, 'string'],
  file_description: [17, 'string'], short_description: [18, 'string'], time_created: [19, 'uint32'],
  time_updated: [20, 'uint32'], subscriptions: [36, 'uint32'], tags: [52, FileTag, true],
};
const QueryFilesRequest = {
  query_type: [1, 'uint32'], page: [2, 'uint32'], numperpage: [3, 'uint32'], appid: [5, 'uint32'],
  requiredtags: [6, 'string', true], search_text: [11, 'string'], return_tags: [18, 'bool'],
  return_short_description: [22, 'bool'], return_details: [37, 'bool'], strip_description_bbcode: [38, 'bool'],
};
const QueryFilesResponse = { total: [1, 'uint32'], publishedfiledetails: [2, FileDetails, true] };

export const QUERY = { votes: 0, newest: 1, trend: 3, search: 11, subscriptions: 12 };

const RESULTS = {
  2: 'failed', 5: 'the sign-in was not accepted', 15: 'access denied', 16: 'timed out', 20: 'Steam is unavailable',
  42: 'no match', 43: 'the account is disabled', 48: 'try again later', 50: 'this account is signed in elsewhere',
  84: 'too many attempts; wait before trying again', 87: 'too many attempts; wait before trying again',
  9: 'not found', 27: 'expired',
};

export class SteamError extends Error {
  constructor(what, result) {
    super(what + ': ' + (RESULTS[result] || 'result ' + result));
    this.result = result;
  }
}

async function gunzip(bytes) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

export function steamIdOfToken(token) {
  const part = token.split('.')[1].replace(/-/g, '+').replace(/_/g, '/');
  return BigInt(JSON.parse(atob(part)).sub);
}

// a session with a Steam connection server, through the relay's WebSocket
export class SteamSession {
  constructor(address) {
    this.address = address;
    this.socket = null;
    this.steamId = 0n;
    this.sessionId = 0;
    this.nextJob = 1n;
    this.jobs = new Map(); // job → {resolve, reject, parts}
    this.waiting = new Map(); // message kind → [resolve]
    this.licenses = null;
    this.heartbeat = null;
    this.loggedOn = false;
    this.onclose = () => {};
  }

  connect() {
    return new Promise((resolve, reject) => {
      const socket = new WebSocket(this.address);
      socket.binaryType = 'arraybuffer';
      this.socket = socket;
      socket.onopen = () => {
        this.send(EMSG.ClientHello, {}, encode(Hello, { protocol_version: PROTOCOL_VERSION }));
        resolve();
      };
      socket.onerror = () => reject(new Error('the relay cannot be reached, or it could not reach Steam'));
      socket.onmessage = (event) => {
        this.packet(new Uint8Array(event.data)).catch((error) => console.error('steam:', error));
      };
      socket.onclose = () => {
        clearInterval(this.heartbeat);
        this.loggedOn = false;
        const error = new Error('the connection to Steam closed');
        for (const job of this.jobs.values()) job.reject(error);
        this.jobs.clear();
        reject(error);
        this.onclose();
      };
    });
  }

  close() {
    if (this.socket) this.socket.close();
  }

  send(kind, header, body) {
    const head = encode(Header, { steamid: this.steamId, client_sessionid: this.sessionId, ...header });
    const packet = new Uint8Array(8 + head.length + body.length);
    const view = new DataView(packet.buffer);
    view.setUint32(0, (kind | PROTO_MASK) >>> 0, true);
    view.setUint32(4, head.length, true);
    packet.set(head, 8);
    packet.set(body, 8 + head.length);
    this.socket.send(packet);
  }

  async packet(packet) {
    if (packet.length < 8) return;
    const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
    const raw = view.getUint32(0, true);
    if (!(raw & PROTO_MASK)) return; // (the old layout: nothing here needs it)
    const kind = raw & ~PROTO_MASK;
    const headLength = view.getUint32(4, true);
    const header = decode(Header, packet.subarray(8, 8 + headLength));
    const body = packet.subarray(8 + headLength);

    if (kind === EMSG.Multi) {
      const multi = decode(Multi, body);
      let inner = multi.message_body || new Uint8Array(0);
      if (multi.size_unzipped) inner = await gunzip(inner);
      const innerView = new DataView(inner.buffer, inner.byteOffset, inner.byteLength);
      for (let at = 0; at + 4 <= inner.length;) {
        const length = innerView.getUint32(at, true);
        await this.packet(inner.subarray(at + 4, at + 4 + length));
        at += 4 + length;
      }
      return;
    }

    if (kind === EMSG.ClientLogOnResponse) {
      const response = decode(LogonResponse, body);
      if (response.eresult === 1) {
        this.steamId = header.steamid;
        this.sessionId = header.client_sessionid;
        this.loggedOn = true;
        const seconds = response.heartbeat_seconds > 0 ? response.heartbeat_seconds : 9;
        this.heartbeat = setInterval(() => this.send(EMSG.ClientHeartBeat, {}, new Uint8Array(0)), seconds * 1000);
      }
    } else if (kind === EMSG.ClientLicenseList) {
      this.licenses = decode(LicenseList, body).licenses;
    } else if (kind === EMSG.ClientLoggedOff) {
      this.loggedOn = false;
      this.loggedOff = decode(LoggedOff, body).eresult;
      this.close();
    }

    const job = header.jobid_target !== undefined ? this.jobs.get(header.jobid_target) : null;
    if (job) {
      if (job.each) {
        // (an answer in parts: each() says when it was the last)
        if (job.each(header, body)) {
          this.jobs.delete(header.jobid_target);
          job.resolve();
        }
      } else {
        this.jobs.delete(header.jobid_target);
        job.resolve({ header, body });
      }
    }
    const waiting = this.waiting.get(kind);
    if (waiting) {
      this.waiting.delete(kind);
      for (const resolve of waiting) resolve({ header, body });
    }
  }

  // the next message of a kind
  next(kind, seconds = 30) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Steam did not answer')), seconds * 1000);
      const list = this.waiting.get(kind) || [];
      list.push((message) => {
        clearTimeout(timer);
        resolve(message);
      });
      this.waiting.set(kind, list);
    });
  }

  // a request answered under its job number; each(header, body), when given,
  // takes every part and returns true at the last
  job(kind, header, body, each, seconds = 60) {
    return new Promise((resolve, reject) => {
      const id = this.nextJob++;
      const timer = setTimeout(() => {
        this.jobs.delete(id);
        reject(new Error('Steam did not answer'));
      }, seconds * 1000);
      this.jobs.set(id, {
        each,
        resolve: (value) => { clearTimeout(timer); resolve(value); },
        reject: (error) => { clearTimeout(timer); reject(error); },
      });
      this.send(kind, { ...header, jobid_source: id }, body);
    });
  }

  // one of Steam's services' methods ("Service.Method#1")
  async call(method, requestSchema, request, responseSchema) {
    const kind = this.loggedOn ? EMSG.ServiceMethodCallFromClient : EMSG.ServiceMethodCallFromClientNonAuthed;
    const { header, body } = await this.job(kind, { target_job_name: method }, encode(requestSchema, request));
    if (header.eresult !== undefined && header.eresult !== 1) throw new SteamError(method.split('#')[0], header.eresult);
    return decode(responseSchema, body);
  }

  async logon(fields, steamId) {
    this.steamId = steamId;
    this.sessionId = 0;
    const answer = this.next(EMSG.ClientLogOnResponse);
    this.send(EMSG.ClientLogon, {}, encode(Logon, {
      protocol_version: PROTOCOL_VERSION, cell_id: 0, client_language: 'english', client_os_type: 20,
      supports_rate_limit_response: true, ...fields }));
    const result = decode(LogonResponse, (await answer).body).eresult;
    if (result !== 1) throw new SteamError('Signing in', result);
  }

  // with no account (what SteamCMD's "login anonymous" is): public product
  // information and Workshop listings, no owned content
  logonAnonymous() {
    return this.logon({}, 0x01a0000000000000n);
  }

  // with the token a QR sign-in gave
  async logonToken(token) {
    this.licenses = null;
    const licenses = this.next(EMSG.ClientLicenseList, 30).catch(() => null);
    await this.logon({ access_token: token, machine_name: 'Halo Content Evolved test page' }, steamIdOfToken(token));
    await licenses;
  }

  // a QR sign-in: show(address) is called with each address to draw (it
  // changes every half minute or so); resolves with the token once the Steam
  // mobile app approves, rejects when cancelled() says so or it runs out
  async signInByQr(show, cancelled = () => false) {
    const name = 'Halo Content Evolved (Workshop test page)';
    const begin = await this.call('Authentication.BeginAuthSessionViaQR#1', BeginQrRequest, {
      device_friendly_name: name, platform_type: 1, website_id: 'Client',
      device_details: { device_friendly_name: name, platform_type: 1, os_type: 20, gaming_device_type: 1 },
    }, BeginQrResponse);
    let clientId = begin.client_id;
    show(begin.challenge_url);
    const wait = Math.max(1, Math.min(10, begin.interval || 5)) * 1000;
    for (;;) {
      await new Promise((resolve) => setTimeout(resolve, wait));
      if (cancelled()) throw new Error('cancelled');
      const poll = await this.call('Authentication.PollAuthSessionStatus#1', PollRequest,
        { client_id: clientId, request_id: begin.request_id }, PollResponse);
      if (poll.new_client_id) clientId = poll.new_client_id;
      if (poll.new_challenge_url) show(poll.new_challenge_url);
      if (poll.refresh_token) return { token: poll.refresh_token, account: poll.account_name || '' };
    }
  }

  // product information: {apps: Map(id → text), packages: Map(id → bytes)}
  async productInfo(apps, packages) {
    const result = { apps: new Map(), packages: new Map(), missing: [] };
    if (!apps.length && !packages.length) return result;
    await this.job(EMSG.ClientPICSProductInfoRequest, {}, encode(ProductInfoRequest, {
      apps: apps.map((app) => ({ id: app.id, access_token: app.token || 0n })),
      packages: packages.map((item) => ({ id: item.id, access_token: item.token || 0n })),
      meta_data_only: false,
    }), (header, body) => {
      const part = decode(ProductInfoResponse, body);
      for (const app of part.apps) {
        if (app.missing_token) result.missing.push(app.id);
        if (app.buffer) result.apps.set(app.id, decoder.decode(app.buffer));
      }
      for (const item of part.packages) if (item.buffer) result.packages.set(item.id, item.buffer);
      return !part.response_pending;
    });
    return result;
  }

  // an app's product information as an object (its "appinfo" section)
  async appInfo(appId) {
    const { body } = await this.job(EMSG.ClientPICSAccessTokenRequest, {}, encode(AccessTokenRequest, { appids: [appId] }));
    const tokens = decode(AccessTokenResponse, body).app_access_tokens;
    const token = tokens.length ? tokens[0].access_token : 0n;
    const info = await this.productInfo([{ id: appId, token }], []);
    const text = info.apps.get(appId);
    if (!text) throw new Error('Steam gave no information for app ' + appId);
    return parseVdf(text).appinfo || {};
  }

  // the apps and depots the account's licences grant: {apps: Set, depots: Set}
  async owned() {
    const owned = { apps: new Set(), depots: new Set(), packages: 0 };
    const licenses = this.licenses || [];
    owned.packages = licenses.length;
    for (let at = 0; at < licenses.length; at += 500) {
      const some = licenses.slice(at, at + 500).map((license) => ({ id: license.package_id, token: license.access_token }));
      const info = await this.productInfo([], some);
      for (const buffer of info.packages.values()) {
        const fields = Object.values(parseBinaryVdf(buffer.subarray(4)))[0] || {};
        for (const app of Object.values(fields.appids || {})) owned.apps.add(Number(app));
        for (const depot of Object.values(fields.depotids || {})) owned.depots.add(Number(depot));
      }
    }
    return owned;
  }

  // a depot's key; null when the account may not have it
  async depotKey(depotId, appId) {
    const { body } = await this.job(EMSG.ClientGetDepotDecryptionKey, {}, encode(DepotKeyRequest, { depot_id: depotId, app_id: appId }));
    const response = decode(DepotKeyResponse, body);
    if (response.eresult !== 1 || !response.depot_encryption_key || response.depot_encryption_key.length !== 32) return null;
    return response.depot_encryption_key.slice();
  }

  async manifestRequestCode(appId, depotId, manifestId) {
    const response = await this.call('ContentServerDirectory.GetManifestRequestCode#1', RequestCodeRequest,
      { app_id: appId, depot_id: depotId, manifest_id: manifestId, app_branch: 'public' }, RequestCodeResponse);
    if (!response.manifest_request_code) throw new Error('Steam gave no code for the manifest (not owned?)');
    return response.manifest_request_code;
  }

  // a page of an app's Workshop: {total, items}
  async workshop(appId, { sort = QUERY.trend, page = 1, count = 20, search = '', tags = [] } = {}) {
    const response = await this.call('PublishedFile.QueryFiles#1', QueryFilesRequest, {
      query_type: search ? QUERY.search : sort, page, numperpage: count, appid: appId, requiredtags: tags,
      search_text: search || undefined, return_tags: true, return_short_description: true, return_details: true,
      strip_description_bbcode: true,
    }, QueryFilesResponse);
    return { total: response.total || 0, items: response.publishedfiledetails };
  }
}

// ---------- Valve's key-value formats

// the text form: "key" "value" and "key" { ... }
export function parseVdf(text) {
  let at = 0;
  function token() {
    for (;;) {
      while (at < text.length && /\s/.test(text[at])) at++;
      if (text[at] === '/' && text[at + 1] === '/') {
        while (at < text.length && text[at] !== '\n') at++;
        continue;
      }
      break;
    }
    if (at >= text.length || text[at] === '\0') return null;
    const first = text[at];
    if (first === '{' || first === '}') {
      at++;
      return first;
    }
    if (first === '"') {
      let value = '';
      at++;
      while (at < text.length && text[at] !== '"') {
        if (text[at] === '\\' && at + 1 < text.length) {
          at++;
          value += text[at] === 'n' ? '\n' : text[at] === 't' ? '\t' : text[at];
        } else {
          value += text[at];
        }
        at++;
      }
      at++;
      return { value };
    }
    let value = '';
    while (at < text.length && !/[\s{}"]/.test(text[at])) value += text[at++];
    return { value };
  }
  function object(depth) {
    if (depth > 32) throw new Error('product information nests too deep');
    const result = {};
    for (;;) {
      const key = token();
      if (key === null || key === '}') return result;
      if (key === '{') throw new Error('product information is malformed');
      const value = token();
      if (value === '{') result[key.value] = object(depth + 1);
      else if (value && value !== '}') result[key.value] = value.value;
      else return result;
    }
  }
  return object(0);
}

// the binary form (a package's product information)
export function parseBinaryVdf(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let at = 0;
  function string() {
    const start = at;
    while (at < bytes.length && bytes[at] !== 0) at++;
    const value = decoder.decode(bytes.subarray(start, at));
    at++;
    return value;
  }
  function object(depth) {
    if (depth > 32) throw new Error('product information nests too deep');
    const result = {};
    while (at < bytes.length) {
      const type = bytes[at++];
      if (type === 8 || type === 11) break;
      const key = string();
      if (type === 0) result[key] = object(depth + 1);
      else if (type === 1) result[key] = string();
      else if (type === 2 || type === 4 || type === 6) { result[key] = view.getInt32(at, true); at += 4; }
      else if (type === 3) { result[key] = view.getFloat32(at, true); at += 4; }
      else if (type === 7 || type === 10) { result[key] = view.getBigUint64(at, true); at += 8; }
      else throw new Error('product information has a value of an unknown kind');
    }
    return result;
  }
  return object(0);
}

// ---------- depot content

const ManifestPayload = {
  mappings: [1, {
    filename: [1, 'string'], size: [2, 'uint64'], flags: [3, 'uint32'], sha_content: [5, 'bytes'],
    chunks: [6, { sha: [1, 'bytes'], offset: [3, 'uint64'], cb_original: [4, 'uint32'], cb_compressed: [5, 'uint32'] }, true],
  }, true],
};
const ManifestMetadata = { depot_id: [1, 'uint32'], gid_manifest: [2, 'uint64'], filenames_encrypted: [4, 'bool'] };
const FLAG_DIRECTORY = 64;

function hex(bytes) {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}

async function inflateRaw(bytes) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

// the one file of a zip (a manifest's, or an old chunk's)
async function unzipOne(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let end = bytes.length - 22;
  while (end >= 0 && view.getUint32(end, true) !== 0x06054b50) end--;
  if (end < 0) throw new Error('not a zip');
  const central = view.getUint32(end + 16, true);
  if (central + 46 > bytes.length || view.getUint32(central, true) !== 0x02014b50) throw new Error('not a zip');
  const method = view.getUint16(central + 10, true);
  const packed = view.getUint32(central + 20, true);
  const local = view.getUint32(central + 42, true);
  if (local + 30 > bytes.length || view.getUint32(local, true) !== 0x04034b50) throw new Error('not a zip');
  const start = local + 30 + view.getUint16(local + 26, true) + view.getUint16(local + 28, true);
  if (start + packed > bytes.length) throw new Error('a zip is cut short');
  const data = bytes.subarray(start, start + packed);
  if (method === 0) return data;
  if (method !== 8) throw new Error('a zip is packed in a way this page does not read');
  return inflateRaw(data);
}

function base64Bytes(text) {
  const binary = atob(text.replace(/\s/g, ''));
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes;
}

// a depot of an app (or a Workshop item, a manifest of the app's Workshop
// depot), with the key the session gave
export class Depot {
  constructor(relay, depotId, keyBytes) {
    this.relay = relay.replace(/\/$/, '');
    this.depotId = depotId;
    this.keyBytes = keyBytes;
    this.key = null;
  }

  async cbcKey() {
    if (!this.key) this.key = await crypto.subtle.importKey('raw', this.keyBytes, 'AES-CBC', false, ['encrypt', 'decrypt']);
    return this.key;
  }

  // Steam's symmetric layout: the vector, encrypted alone (ECB), then the
  // data (CBC, padded). Web Crypto has no ECB: one block is decrypted as CBC
  // with a zero vector and a second block made to unpad to nothing.
  async decrypt(bytes) {
    if (bytes.length < 32 || bytes.length % 16) throw new Error('encrypted data of a wrong size');
    const key = await this.cbcKey();
    const first = bytes.subarray(0, 16);
    const pad = new Uint8Array(16).fill(16);
    const tail = new Uint8Array(await crypto.subtle.encrypt({ name: 'AES-CBC', iv: first }, key, pad)).subarray(0, 16);
    const both = new Uint8Array(32);
    both.set(first, 0);
    both.set(tail, 16);
    const iv = new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-CBC', iv: new Uint8Array(16) }, key, both));
    return new Uint8Array(await crypto.subtle.decrypt({ name: 'AES-CBC', iv }, key, bytes.subarray(16)));
  }

  async get(path, signal) {
    let last;
    for (let attempt = 0; attempt < 4; attempt++) {
      try {
        const response = await fetch(this.relay + '/cdn/depot/' + this.depotId + '/' + path, { signal, cache: 'no-store' });
        if (response.ok) return new Uint8Array(await response.arrayBuffer());
        last = new Error('Steam content server: HTTP ' + response.status);
        if (response.status === 401 || response.status === 403 || response.status === 404) break;
      } catch (error) {
        if (signal && signal.aborted) throw error;
        last = error;
      }
      await new Promise((resolve) => setTimeout(resolve, 500 * (attempt + 1)));
    }
    throw last;
  }

  // a manifest's files: [{name, size, sha, chunks: [{sha, offset, size, packed}]}]
  async manifest(manifestId, requestCode) {
    const content = await unzipOne(await this.get('manifest/' + manifestId + '/5/' + requestCode));
    const view = new DataView(content.buffer, content.byteOffset, content.byteLength);
    let payload = null;
    let metadata = {};
    for (let at = 0; at + 8 <= content.length;) {
      const magic = view.getUint32(at, true);
      if (magic === 0x32c415ab) break;
      const length = view.getUint32(at + 4, true);
      const section = content.subarray(at + 8, at + 8 + length);
      if (magic === 0x71f617d0) payload = decode(ManifestPayload, section);
      else if (magic === 0x1f4812be) metadata = decode(ManifestMetadata, section);
      at += 8 + length;
    }
    if (!payload) throw new Error('the manifest has no file list');
    const files = [];
    for (const mapping of payload.mappings) {
      if ((mapping.flags || 0) & FLAG_DIRECTORY) continue;
      let name = mapping.filename || '';
      if (metadata.filenames_encrypted) {
        name = decoder.decode(await this.decrypt(base64Bytes(name))).replace(/\0+$/, '');
      }
      files.push({
        name: name.replace(/\\/g, '/'),
        size: Number(mapping.size || 0n),
        sha: mapping.sha_content ? hex(mapping.sha_content) : '',
        chunks: mapping.chunks.map((chunk) => ({
          sha: hex(chunk.sha), offset: Number(chunk.offset || 0n), size: chunk.cb_original || 0, packed: chunk.cb_compressed || 0,
        })).sort((a, b) => a.offset - b.offset),
      });
    }
    files.sort((a, b) => a.name.localeCompare(b.name));
    return files;
  }

  // a chunk's bytes, checked against its SHA-1
  async chunk(chunk, signal) {
    const data = await this.decrypt(await this.get('chunk/' + chunk.sha, signal));
    let bytes;
    if (data[0] === 0x56 && data[1] === 0x5a && data[2] === 0x61) {
      // "VZa", four bytes, the LZMA properties, the stream, a ten-byte footer
      bytes = lzmaDecode(data.subarray(7, 12), data.subarray(12, data.length - 10), chunk.size);
    } else if (data[0] === 0x56 && data[1] === 0x53 && data[2] === 0x5a && data[3] === 0x61) {
      // "VSZa", four bytes, a Zstandard frame, a fifteen-byte footer
      bytes = zstdDecode(data.subarray(8, data.length - 15), new Uint8Array(chunk.size));
    } else if (data[0] === 0x50 && data[1] === 0x4b) {
      bytes = await unzipOne(data);
    } else {
      throw new Error('a chunk is packed in a way this page does not read');
    }
    if (bytes.length !== chunk.size) throw new Error('a chunk unpacked to a wrong size');
    if (hex(new Uint8Array(await crypto.subtle.digest('SHA-1', bytes))) !== chunk.sha) throw new Error('a chunk failed its check');
    return bytes;
  }

  // a file into write(bytes, offset), several chunks at a time;
  // progress(bytes done)
  async download(file, write, { signal, progress = () => {}, parallel = 6 } = {}) {
    let next = 0;
    let done = 0;
    const worker = async () => {
      while (next < file.chunks.length) {
        if (signal && signal.aborted) throw new DOMException('cancelled', 'AbortError');
        const chunk = file.chunks[next++];
        const bytes = await this.chunk(chunk, signal);
        await write(bytes, chunk.offset);
        done += bytes.length;
        progress(done);
      }
    };
    await Promise.all(Array.from({ length: Math.min(parallel, file.chunks.length || 1) }, worker));
  }
}
