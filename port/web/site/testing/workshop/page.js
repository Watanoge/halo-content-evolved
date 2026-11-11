/*
PAGE.JS

The Steam Workshop test page (/testing/workshop; nothing links to it): a
sign-in to Steam by QR code, the check that the account has Halo: Combat
Evolved Anniversary, the download of the resource maps Master Chief
Collection maps need, and the Halo 1 Workshop with each item's files to
choose and download. steam.js has the protocol; tools/steam_relay.mjs is the
relay it needs, on the player's own computer.

- ?relay=ADDRESS names a relay other than this page's own origin or
  http://localhost:8791.
*/

import { SteamSession, Depot, QUERY } from './steam.js';
import { drawQr } from './qr.js';

const MCC_APP = 976730;
const HALO1_APP = 1064221; // Halo: Combat Evolved Anniversary, a DLC of it
const HALO1_TAG = 'Halo1';
const TOKEN_KEY = 'steam-test-token';
const PER_PAGE = 10;

// the files of Halo 1's depot that MCC maps need, and where OpenCE looks for
// them (docs/mcc_maps.md)
const BASE_FILES = [
  { from: 'halo1/maps/bitmaps.map', to: 'mcc_maps/_shared/singleplayer/bitmaps.map' },
  { from: 'halo1/maps/custom_edition/bitmaps.map', to: 'mcc_maps/_shared/multiplayer/bitmaps.map' },
  { from: 'halo1/maps/custom_edition/sounds.map', to: 'mcc_maps/_shared/multiplayer/sounds.map' },
];

const $ = (id) => document.getElementById(id);

function log(...words) {
  const line = words.join(' ');
  console.log(line);
  $('log').textContent += line + '\n';
  $('log').scrollTop = $('log').scrollHeight;
}

function sizeText(bytes) {
  if (bytes >= 1 << 30) return (bytes / (1 << 30)).toFixed(2) + ' GB';
  if (bytes >= 1 << 20) return (bytes / (1 << 20)).toFixed(1) + ' MB';
  if (bytes >= 1024) return (bytes / 1024).toFixed(0) + ' KB';
  return bytes + ' B';
}

function element(tag, properties = {}, children = []) {
  const node = Object.assign(document.createElement(tag), properties);
  for (const child of children) node.append(child);
  return node;
}

// ---------- the relay

let relay = '';

async function relayAnswers(address) {
  try {
    const response = await fetch(address + '/relay', { cache: 'no-store', signal: AbortSignal.timeout(3000) });
    return response.ok && (await response.json()).relay === 'steam';
  } catch {
    return false;
  }
}

async function findRelay() {
  const named = new URLSearchParams(location.search).get('relay') || $('relay-address').value.trim();
  const candidates = [];
  if (named) candidates.push(named.replace(/\/$/, ''));
  candidates.push(location.origin, 'http://localhost:8791');
  $('relay-status').textContent = 'Looking for the relay…';
  for (const candidate of candidates) {
    if (await relayAnswers(candidate)) {
      relay = candidate;
      $('relay-status').textContent = 'Relay: ' + candidate;
      $('relay-help').hidden = true;
      $('account-panel').hidden = false;
      resume();
      return;
    }
  }
  $('relay-status').textContent = 'No relay answers.';
  $('relay-status').className = 'bad';
  $('relay-address').value = named || 'http://localhost:8791';
  $('relay-help').hidden = false;
}

$('relay-retry').addEventListener('click', () => {
  $('relay-status').className = '';
  findRelay();
});

// ---------- the session

let session = null;
let signingIn = false;
let cancelSignIn = false;

function socketAddress() {
  return relay.replace(/^http/, 'ws') + '/cm';
}

function signedOut(message) {
  for (const id of ['owned-panel', 'folder-panel', 'base-panel', 'workshop-panel']) $(id).hidden = true;
  $('sign-in').hidden = false;
  $('sign-out').hidden = true;
  $('qr-box').hidden = true;
  $('account-status').textContent = message || 'Not signed in.';
}

async function open() {
  if (session) session.close();
  const opened = new SteamSession(socketAddress());
  opened.onclose = () => {
    if (session !== opened) return;
    session = null;
    if (!signingIn) signedOut(opened.loggedOff ? 'Steam ended the session (result ' + opened.loggedOff + ').' : 'The connection to Steam closed.');
  };
  await opened.connect();
  session = opened;
  return opened;
}

async function signedIn(token, account) {
  $('account-status').textContent = 'Signing in…';
  await session.logonToken(token);
  try {
    sessionStorage.setItem(TOKEN_KEY, JSON.stringify({ token, account }));
  } catch {
    // (no storage: the sign-in lasts until the page goes)
  }
  $('account-status').textContent = 'Signed in' + (account ? ' as ' + account : '') + ' (' + session.steamId + ').';
  $('sign-in').hidden = true;
  $('sign-out').hidden = false;
  $('qr-box').hidden = true;
  log('signed in;', (session.licenses || []).length, 'licences');
  afterSignIn().catch((error) => {
    $('account-status').textContent += ' The account could not be checked: ' + error.message;
    log('after sign-in:', error.message);
  });
}

// a sign-in kept from before a reload of this tab
async function resume() {
  let kept = null;
  try {
    kept = JSON.parse(sessionStorage.getItem(TOKEN_KEY) || 'null');
  } catch {
    kept = null;
  }
  if (!kept || !kept.token) return;
  signingIn = true;
  try {
    await open();
    await signedIn(kept.token, kept.account);
  } catch (error) {
    log('the kept sign-in failed:', error.message);
    try {
      sessionStorage.removeItem(TOKEN_KEY);
    } catch {
      // (no storage)
    }
    if (session) session.close();
    signedOut('Sign in again: ' + error.message);
  } finally {
    signingIn = false;
  }
}

$('sign-in').addEventListener('click', async () => {
  if (signingIn) return;
  signingIn = true;
  cancelSignIn = false;
  $('sign-in').disabled = true;
  try {
    $('account-status').textContent = 'Connecting to Steam…';
    await open();
    const { token, account } = await session.signInByQr((address) => {
      drawQr($('qr'), address);
      $('qr-link').href = address;
      $('qr-box').hidden = false;
      $('account-status').textContent = 'Waiting for the Steam app to approve…';
    }, () => cancelSignIn);
    await signedIn(token, account);
  } catch (error) {
    if (session) session.close();
    signedOut(error.message === 'cancelled' ? 'Not signed in.' : 'Sign-in failed: ' + error.message);
  } finally {
    signingIn = false;
    $('sign-in').disabled = false;
  }
});

$('qr-cancel').addEventListener('click', () => {
  cancelSignIn = true;
  $('qr-box').hidden = true;
});

$('sign-out').addEventListener('click', () => {
  try {
    sessionStorage.removeItem(TOKEN_KEY);
  } catch {
    // (no storage)
  }
  signingIn = true; // (so that the close says nothing of its own)
  if (session) session.close();
  session = null;
  signingIn = false;
  signedOut();
});

// ---------- where the files go: a folder the player chooses, or (a browser
// that cannot choose one) the browser's own storage, saved from there

const folder = { handle: null, chosen: false };

async function browserFolder() {
  const root = await navigator.storage.getDirectory();
  return root.getDirectoryHandle('steam-test', { create: true });
}

async function showFolder() {
  $('folder-panel').hidden = false;
  if (folder.chosen) {
    $('folder-status').textContent = 'Folder: ' + folder.handle.name;
  } else if (window.showDirectoryPicker) {
    $('folder-status').textContent = 'No folder chosen yet. Downloads ask for one.';
  } else {
    $('folder-choose').hidden = true;
    $('folder-status').textContent = 'This browser cannot write into a folder: files are kept in the browser\'s ' +
      'storage, with a Save link for each. Chrome or Edge write into a folder directly.';
  }
}

async function chooseFolder() {
  folder.handle = await window.showDirectoryPicker({ mode: 'readwrite', id: 'steam-test' });
  folder.chosen = true;
  showFolder();
}

$('folder-choose').addEventListener('click', () => chooseFolder().catch(() => {}));

async function destination() {
  if (folder.chosen) return folder.handle;
  if (window.showDirectoryPicker) {
    await chooseFolder();
    return folder.handle;
  }
  if (!folder.handle) folder.handle = await browserFolder();
  return folder.handle;
}

// a path of ours or of a manifest's, as names safe to make
function pathParts(path) {
  const parts = path.split('/').filter((part) => part !== '');
  if (!parts.length || parts.length > 16) throw new Error('a file has a path this page does not take: ' + path);
  for (const part of parts) {
    if (part === '.' || part === '..' || part.length > 200 || /[\\:*?"<>|\x00-\x1f]/.test(part)) {
      throw new Error('a file has a path this page does not take: ' + path);
    }
  }
  return parts;
}

// a depot's file into the folder at a path; resolves with its handle
async function saveFile(depot, file, path, signal, progress) {
  const parts = pathParts(path);
  let directory = await destination();
  for (const part of parts.slice(0, -1)) directory = await directory.getDirectoryHandle(part, { create: true });
  const handle = await directory.getFileHandle(parts[parts.length - 1], { create: true });
  const writable = await handle.createWritable();
  try {
    await writable.truncate(file.size);
    await depot.download(file, (bytes, offset) => writable.write({ type: 'write', position: offset, data: bytes }),
      { signal, progress });
    await writable.close();
  } catch (error) {
    await writable.abort().catch(() => {});
    throw error;
  }
  return handle;
}

// (a browser that kept the file in its own storage: a link that saves it)
async function saveLink(handle, name) {
  if (folder.chosen) return null;
  const link = element('a', { textContent: 'Save', download: name });
  link.href = URL.createObjectURL(await handle.getFile());
  return link;
}

// several files, one after another, with a bar and a line of status
async function saveFiles(depot, files, { bar, status, signal, links }) {
  const total = files.reduce((sum, entry) => sum + entry.file.size, 0);
  let before = 0;
  const started = Date.now();
  for (const entry of files) {
    const handle = await saveFile(depot, entry.file, entry.to, signal, (done) => {
      const all = before + done;
      const rate = all / Math.max(1, (Date.now() - started) / 1000);
      bar.style.width = (total ? (100 * all) / total : 100) + '%';
      status.textContent = entry.to + ': ' + sizeText(all) + ' of ' + sizeText(total) + ' (' + sizeText(rate) + '/s)';
    });
    before += entry.file.size;
    log('saved', entry.to, sizeText(entry.file.size));
    const link = await saveLink(handle, entry.to.split('/').pop());
    if (link && links) links(entry, link);
  }
  bar.style.width = '100%';
  status.textContent = 'Done: ' + files.length + (files.length === 1 ? ' file, ' : ' files, ') + sizeText(total) +
    (folder.chosen ? ' in ' + folder.handle.name : '') + '.';
}

// ---------- ownership and Halo 1's files

let workshopDepot = null; // (the app's Workshop depot, once its key is had)
let base = null; // {depot, files}

function verdict(id, yes, yesText = 'Yes', noText = 'No') {
  $(id).textContent = yes ? yesText : noText;
  $(id).className = yes ? 'good' : 'bad';
}

async function afterSignIn() {
  $('owned-panel').hidden = false;
  for (const id of ['owned-mcc', 'owned-halo1', 'owned-files']) {
    $(id).textContent = '…';
    $(id).className = 'dim';
  }
  base = null;
  workshopDepot = null;

  const [owned, info] = await Promise.all([session.owned(), session.appInfo(MCC_APP)]);
  log('licences:', owned.packages, 'packages,', owned.apps.size, 'apps,', owned.depots.size, 'depots');

  // Halo 1's depot is the one the app's information marks with its DLC
  const depots = Object.entries(info.depots || {}).filter(([id, depot]) => /^\d+$/.test(id) && depot.manifests &&
    depot.manifests.public).map(([id, depot]) => ({
    id: Number(id), dlc: Number(depot.dlcappid || 0),
    manifest: typeof depot.manifests.public === 'string' ? depot.manifests.public : depot.manifests.public.gid,
  }));
  const halo1 = depots.find((depot) => depot.dlc === HALO1_APP);
  const key = halo1 ? await session.depotKey(halo1.id, MCC_APP) : null;

  // a family's shared copy is in no licence of the account's own: Steam
  // giving the key is what decides
  const ownsMcc = owned.apps.has(MCC_APP);
  const ownsHalo1 = owned.apps.has(HALO1_APP) || (halo1 && owned.depots.has(halo1.id));
  verdict('owned-mcc', ownsMcc, 'Owned', key ? 'Not in this account\'s own licences (shared?)' : 'Not owned');
  verdict('owned-halo1', ownsHalo1, 'Owned', key ? 'Not in this account\'s own licences (shared?)' : 'Not owned');
  verdict('owned-files', !!key);
  log('Halo 1 depot:', halo1 ? halo1.id + ' manifest ' + halo1.manifest : 'not found', '| key:', key ? 'yes' : 'no');

  showFolder();
  if (key) {
    $('base-panel').hidden = false;
    loadBase(halo1, key).catch((error) => {
      $('base-status').textContent = 'The file list could not be read: ' + error.message;
      log('base files:', error.message);
    });
  }
  $('workshop-panel').hidden = false;
  showWorkshop(1);
}

async function loadBase(halo1, key) {
  $('base-status').textContent = 'Reading Halo 1\'s file list…';
  $('base-files').replaceChildren();
  const depot = new Depot(relay, halo1.id, key);
  const code = await session.manifestRequestCode(MCC_APP, halo1.id, BigInt(halo1.manifest));
  const files = await depot.manifest(halo1.manifest, code);
  log('Halo 1 depot:', files.length, 'files');
  const byName = new Map(files.map((file) => [file.name.toLowerCase(), file]));
  const found = [];
  for (const wanted of BASE_FILES) {
    const file = byName.get(wanted.from);
    const status = element('td', { className: file ? 'dim' : 'bad', textContent: file ? '' : 'not in the depot' });
    $('base-files').append(element('tr', {}, [
      element('td', {}, [element('code', { textContent: wanted.from })]),
      element('td', { className: 'size', textContent: file ? sizeText(file.size) : '' }), status]));
    if (file) found.push({ file, to: wanted.to, cell: status });
  }
  base = { depot, files: found };
  $('base-download').disabled = !found.length;
  $('base-status').textContent = found.length ? '' : 'None of the files are in this depot.';
}

let baseAbort = null;

$('base-download').addEventListener('click', async () => {
  if (!base || baseAbort) return;
  baseAbort = new AbortController();
  $('base-download').disabled = true;
  $('base-cancel').hidden = false;
  try {
    await saveFiles(base.depot, base.files, {
      bar: $('base-bar'), status: $('base-status'), signal: baseAbort.signal,
      links: (entry, link) => entry.cell.replaceChildren(link),
    });
  } catch (error) {
    $('base-status').textContent = error.name === 'AbortError' ? 'Cancelled.' : 'Failed: ' + error.message;
    log('base files:', error.message);
  } finally {
    baseAbort = null;
    $('base-download').disabled = false;
    $('base-cancel').hidden = true;
  }
});

$('base-cancel').addEventListener('click', () => baseAbort && baseAbort.abort());

// ---------- the Workshop

let workshopPage = 1;

async function itemDepot() {
  if (!workshopDepot) {
    const key = await session.depotKey(MCC_APP, MCC_APP);
    if (!key) throw new Error('Steam does not give this account the Workshop\'s files');
    workshopDepot = new Depot(relay, MCC_APP, key);
  }
  return workshopDepot;
}

// what a file of an item is, for the choices
function fileKind(name) {
  const lower = name.toLowerCase();
  const leaf = lower.split('/').pop();
  if (leaf === 'bitmaps.map' || leaf === 'sounds.map' || leaf === 'loc.map') return 'resources';
  if (lower.endsWith('.map')) return 'maps';
  if (/\.(json|png|jpg|jpeg|txt)$/.test(lower)) return 'details';
  return 'other';
}

const KINDS = [
  ['maps', 'Maps'], ['resources', 'Their bitmaps and sounds'], ['details', 'Descriptions and pictures'], ['other', 'Other files'],
];

// an item's download settings: its files, by kind and one by one
async function showOptions(item, box, button) {
  if (box.childElementCount) {
    box.hidden = !box.hidden;
    return;
  }
  box.hidden = false;
  box.textContent = 'Reading the item\'s file list…';
  button.disabled = true;
  try {
    const depot = await itemDepot();
    const manifest = String(item.hcontent_file);
    const code = await session.manifestRequestCode(MCC_APP, MCC_APP, BigInt(manifest));
    const files = await depot.manifest(manifest, code);
    box.textContent = '';

    const boxes = [];
    const kinds = element('div', { className: 'row' });
    const list = element('div', { className: 'files' });
    const summary = element('span', { className: 'dim' });
    const update = () => {
      const chosen = boxes.filter((entry) => entry.input.checked);
      summary.textContent = chosen.length + ' of ' + files.length + ' files, ' +
        sizeText(chosen.reduce((sum, entry) => sum + entry.file.size, 0));
      download.disabled = !chosen.length;
    };
    for (const file of files) {
      const kind = fileKind(file.name);
      const input = element('input', { type: 'checkbox', checked: kind !== 'other' });
      const link = element('span');
      input.addEventListener('change', update);
      boxes.push({ file, kind, input, link });
      list.append(element('label', {}, [input, element('span', { textContent: file.name }), link,
        element('span', { className: 'dim', textContent: sizeText(file.size) })]));
    }
    for (const [kind, title] of KINDS) {
      const of = boxes.filter((entry) => entry.kind === kind);
      if (!of.length) continue;
      const input = element('input', { type: 'checkbox', checked: kind !== 'other' });
      input.addEventListener('change', () => {
        for (const entry of of) entry.input.checked = input.checked;
        update();
      });
      kinds.append(element('label', {}, [input, ' ' + title + ' (' + of.length + ', ' +
        sizeText(of.reduce((sum, entry) => sum + entry.file.size, 0)) + ')']));
    }

    const download = element('button', { className: 'main', textContent: 'Download selected' });
    const cancel = element('button', { textContent: 'Cancel', hidden: true });
    const bar = element('div');
    const status = element('p', { className: 'dim' });
    let abort = null;
    download.addEventListener('click', async () => {
      if (abort) return;
      abort = new AbortController();
      download.disabled = true;
      cancel.hidden = false;
      const chosen = boxes.filter((entry) => entry.input.checked)
        .map((entry) => ({ file: entry.file, to: 'workshop/' + item.publishedfileid + '/' + entry.file.name, entry }));
      try {
        await saveFiles(depot, chosen, { bar, status, signal: abort.signal,
          links: (saved, link) => saved.entry.link.replaceChildren(link) });
      } catch (error) {
        status.textContent = error.name === 'AbortError' ? 'Cancelled.' : 'Failed: ' + error.message;
        log('item', String(item.publishedfileid) + ':', error.message);
      } finally {
        abort = null;
        download.disabled = false;
        cancel.hidden = true;
      }
    });
    cancel.addEventListener('click', () => abort && abort.abort());

    box.append(kinds, list, element('div', { className: 'row' }, [download, cancel, summary]),
      element('div', { className: 'bar' }, [bar]), status,
      element('p', { className: 'dim', textContent: 'Saved under workshop/' + item.publishedfileid + '/, laid out as in the item.' }));
    update();
  } catch (error) {
    box.textContent = 'The item\'s files could not be listed: ' + error.message;
    log('item', String(item.publishedfileid) + ':', error.message);
  } finally {
    button.disabled = false;
  }
}

function itemRow(item) {
  const picture = element('img', { alt: '', loading: 'lazy', referrerPolicy: 'no-referrer' });
  if (item.preview_url && item.preview_url.startsWith('https://')) picture.src = item.preview_url;
  picture.addEventListener('error', () => { picture.style.visibility = 'hidden'; });
  const options = element('div', { className: 'options', hidden: true });
  const button = element('button', { textContent: 'Download settings…' });
  button.addEventListener('click', () => showOptions(item, options, button));
  const updated = item.time_updated ? new Date(item.time_updated * 1000).toLocaleDateString() : '';
  const facts = [sizeText(Number(item.file_size || 0n)), (item.subscriptions || 0).toLocaleString() + ' subscribers',
    item.tags.map((tag) => tag.tag).filter((tag) => tag !== HALO1_TAG).join(', '), updated ? 'updated ' + updated : '']
    .filter(Boolean).join(' · ');
  const page = element('a', { textContent: 'Workshop page', target: '_blank', rel: 'noopener noreferrer',
    href: 'https://steamcommunity.com/sharedfiles/filedetails/?id=' + item.publishedfileid });
  return element('div', { className: 'item' }, [picture, element('div', {}, [
    element('h3', { textContent: item.title || '(untitled)' }),
    element('p', { className: 'dim', textContent: facts }),
    element('p', { textContent: (item.short_description || '').slice(0, 220) }),
    element('div', { className: 'row' }, [button, page]),
  ]), options]);
}

async function showWorkshop(page) {
  if (!session) return;
  $('workshop-go').disabled = true;
  try {
    const search = $('workshop-search').value.trim();
    const result = await session.workshop(MCC_APP, {
      sort: Number($('workshop-sort').value) || QUERY.subscriptions, page, count: PER_PAGE, search, tags: [HALO1_TAG] });
    workshopPage = page;
    const pages = Math.max(1, Math.ceil(result.total / PER_PAGE));
    $('workshop-count').textContent = result.total.toLocaleString() + ' items';
    $('workshop-page').textContent = 'Page ' + page + ' of ' + pages;
    $('workshop-previous').disabled = page <= 1;
    $('workshop-next').disabled = page >= pages;
    $('workshop-list').replaceChildren(...result.items.filter((item) => item.hcontent_file).map(itemRow));
    if (!result.items.length) $('workshop-list').textContent = 'Nothing found.';
  } catch (error) {
    $('workshop-list').textContent = 'The Workshop could not be listed: ' + error.message;
    log('workshop:', error.message);
  } finally {
    $('workshop-go').disabled = false;
  }
}

$('workshop-go').addEventListener('click', () => showWorkshop(1));
$('workshop-search').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') showWorkshop(1);
});
$('workshop-sort').addEventListener('change', () => showWorkshop(1));
$('workshop-previous').addEventListener('click', () => showWorkshop(workshopPage - 1));
$('workshop-next').addEventListener('click', () => showWorkshop(workshopPage + 1));

findRelay();
