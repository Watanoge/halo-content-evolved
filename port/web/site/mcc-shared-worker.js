/* MCC's shared resource maps, the ones MCC maps take their bitmaps and sounds
 * from: mcc_maps/_shared/singleplayer/bitmaps.map (MCC's halo1/maps) and
 * mcc_maps/_shared/multiplayer/bitmaps.map + sounds.map (MCC's
 * halo1/maps/custom_edition), as port/linux/game/mcc_resources.c reads them
 * and the Steam download (steam_downloads.c) and Android's importer place
 * them. A selection may be MCC's halo1 or maps folder, its custom_edition
 * folder, or a _shared folder; the files replace those already here. */
'use strict';
if (typeof importScripts === 'function') importScripts('map-import-files.js', 'map-import-storage.js');

(function (root) {
  const files = root.HaloImportFiles || require('./map-import-files.js');
  const TYPES = { 'bitmaps.map': 1, 'sounds.map': 2 };

  // which shared set a resource map belongs to, from the folder it is in
  function kindOf(path) {
    const parts = path.split('/'), name = parts.pop(), parent = parts.pop() || '';
    if (!Object.hasOwn(TYPES, name)) return null;
    if (parent === 'custom_edition' || parent === 'multiplayer') return 'multiplayer';
    if (name === 'sounds.map') return null;   // (the singleplayer set has no sounds.map)
    // a custom_maps folder's or an MCC package's own bitmaps.map is not MCC's
    if (parts.includes('custom_edition') || parts.includes('multiplayer')) return null;
    return 'singleplayer';
  }

  async function plan(list) {
    // (only the resource maps: MCC's halo1 folder has thousands of other files)
    const wanted = (Array.isArray(list) ? list : []).filter(f => /(^|[\\/])(bitmaps|sounds)\.map$/i.test(f.webkitRelativePath || f.name));
    if (!wanted.length) {
      throw new Error("No MCC shared files found: choose MCC's halo1 folder (or its maps or maps/custom_edition folder).");
    }
    const inputs = files.chosen(wanted);
    const found = new Map();
    for (const { path, file } of inputs) {
      const kind = kindOf(path);
      if (!kind) continue;
      const name = path.split('/').pop(), key = kind + '/' + name;
      if (found.has(key)) throw new Error(`More than one ${kind} ${name} is in the selection: choose one MCC install at a time.`);
      files.resourceHeader(await files.bytes(file, 0, 16), file.size, TYPES[name]);
      found.set(key, { kind, name, path: key, size: file.size, file });
    }
    if (!found.size) {
      throw new Error("No MCC shared files found: choose MCC's halo1 folder (or its maps or maps/custom_edition folder).");
    }
    const multiplayer = ['bitmaps.map', 'sounds.map'].filter(n => found.has('multiplayer/' + n));
    if (multiplayer.length === 1) {
      throw new Error(`The multiplayer set needs both bitmaps.map and sounds.map; only ${multiplayer[0]} was found.`);
    }
    const kinds = [...new Set([...found.values()].map(e => e.kind))];
    return { family: 'shared', folder: '_shared', title: kinds.join(' and ') + ' shared files', entries: [...found.values()] };
  }

  root.HaloMccShared = { plan, kindOf };
  if (typeof module !== 'undefined') module.exports = root.HaloMccShared;
  if (typeof WorkerGlobalScope !== 'undefined' && root instanceof WorkerGlobalScope) root.HaloMapStorage.worker(plan);
})(globalThis);
