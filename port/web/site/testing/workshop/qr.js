/*
QR.JS

A QR code of a short text (a sign-in address): qrMatrix(text) gives its
modules as rows of booleans, drawQr(canvas, text) draws it. Byte mode, error
correction level L, versions 1 to 5 (up to 106 bytes), mask 0.
*/

// [data codewords, error correction codewords] of versions 1-5 at level L
// (one block each)
const VERSIONS = [null, [19, 7], [34, 10], [55, 15], [80, 20], [108, 26]];

// the format bits of level L with mask 0, first to last
const FORMAT = '111011111000100';

function errorCorrection(data, count) {
  const exp = new Uint8Array(512);
  const log = new Uint8Array(256);
  for (let i = 0, x = 1; i < 255; i++) {
    exp[i] = x;
    log[x] = i;
    x <<= 1;
    if (x & 0x100) x ^= 0x11d;
  }
  for (let i = 255; i < 512; i++) exp[i] = exp[i - 255];
  const mul = (a, b) => (a && b ? exp[log[a] + log[b]] : 0);
  // the generator: (x - 1)(x - a)(x - a^2)...
  let generator = [1];
  for (let i = 0; i < count; i++) {
    const next = new Array(generator.length + 1).fill(0);
    for (let j = 0; j < generator.length; j++) {
      next[j] ^= generator[j];
      next[j + 1] ^= mul(generator[j], exp[i]);
    }
    generator = next;
  }
  const remainder = new Array(count).fill(0);
  for (const byte of data) {
    const factor = byte ^ remainder.shift();
    remainder.push(0);
    for (let j = 0; j < count; j++) remainder[j] ^= mul(generator[j + 1], factor);
  }
  return remainder;
}

export function qrMatrix(text) {
  const bytes = new TextEncoder().encode(text);
  let version = 1;
  while (version <= 5 && bytes.length + 2 > VERSIONS[version][0]) version++;
  if (version > 5) throw new Error('too long for a QR code here');
  const [dataCount, ecCount] = VERSIONS[version];

  // the mode (bytes), the count, the bytes, the end, then padding
  const bits = [];
  const push = (value, count) => {
    for (let i = count - 1; i >= 0; i--) bits.push((value >> i) & 1);
  };
  push(4, 4);
  push(bytes.length, 8);
  for (const byte of bytes) push(byte, 8);
  push(0, Math.min(4, dataCount * 8 - bits.length));
  while (bits.length % 8) bits.push(0);
  const data = [];
  for (let i = 0; i < bits.length; i += 8) data.push(parseInt(bits.slice(i, i + 8).join(''), 2));
  for (let pad = 0xec; data.length < dataCount; pad ^= 0xec ^ 0x11) data.push(pad);
  const codewords = data.concat(errorCorrection(data, ecCount));

  const size = 17 + 4 * version;
  const dark = Array.from({ length: size }, () => new Array(size).fill(false));
  const fixed = Array.from({ length: size }, () => new Array(size).fill(false));
  const set = (row, column, value) => {
    if (row < 0 || column < 0 || row >= size || column >= size) return;
    dark[row][column] = value;
    fixed[row][column] = true;
  };

  // the three finders with their separators
  for (const [top, left] of [[0, 0], [0, size - 7], [size - 7, 0]]) {
    for (let r = -1; r <= 7; r++) {
      for (let c = -1; c <= 7; c++) {
        const ring = Math.max(Math.abs(r - 3), Math.abs(c - 3));
        set(top + r, left + c, ring !== 2 && ring <= 3);
      }
    }
  }
  // the timing lines
  for (let i = 8; i < size - 8; i++) {
    set(6, i, i % 2 === 0);
    set(i, 6, i % 2 === 0);
  }
  // the alignment pattern (versions 2 to 5 have one)
  if (version >= 2) {
    const centre = size - 7;
    for (let r = -2; r <= 2; r++) {
      for (let c = -2; c <= 2; c++) set(centre + r, centre + c, Math.max(Math.abs(r), Math.abs(c)) !== 1);
    }
  }
  // the format bits, twice, and the module that is always dark
  for (let i = 0; i < 15; i++) {
    const bit = FORMAT[i] === '1';
    if (i < 6) set(8, i, bit);
    else if (i === 6) set(8, 7, bit);
    else if (i === 7) set(8, 8, bit);
    else if (i === 8) set(7, 8, bit);
    else set(14 - i, 8, bit);
    if (i < 7) set(size - 1 - i, 8, bit);
    else set(8, size - 15 + i, bit);
  }
  set(size - 8, 8, true);

  // the codewords, in column pairs from the bottom right, up then down,
  // under mask 0 (the modules whose row + column is even are inverted)
  let index = 0;
  let upward = true;
  for (let right = size - 1; right > 0; right -= 2) {
    if (right === 6) right--;
    for (let step = 0; step < size; step++) {
      const row = upward ? size - 1 - step : step;
      for (const column of [right, right - 1]) {
        if (fixed[row][column]) continue;
        const bit = index < codewords.length * 8 ? (codewords[index >> 3] >> (7 - (index & 7))) & 1 : 0;
        index++;
        dark[row][column] = (bit === 1) !== ((row + column) % 2 === 0);
      }
    }
    upward = !upward;
  }
  return dark;
}

export function drawQr(canvas, text, scale = 6) {
  const matrix = qrMatrix(text);
  const quiet = 4;
  const size = (matrix.length + quiet * 2) * scale;
  canvas.width = size;
  canvas.height = size;
  const context = canvas.getContext('2d');
  context.fillStyle = '#fff';
  context.fillRect(0, 0, size, size);
  context.fillStyle = '#000';
  matrix.forEach((row, r) => row.forEach((on, c) => {
    if (on) context.fillRect((c + quiet) * scale, (r + quiet) * scale, scale, scale);
  }));
}
