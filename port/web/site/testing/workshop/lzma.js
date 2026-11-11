/*
LZMA.JS

An LZMA decoder (the reference decoder's algorithm, for a stream whose size is
known), for the chunks of Steam's depots: lzmaDecode(properties, input, size)
with the stream's five property bytes, its bytes, and the size it unpacks to.
*/

const BIT_MODEL_TOTAL = 2048;
const TOP = 1 << 24;

export function lzmaDecode(properties, input, size) {
  let d = properties[0];
  if (d >= 9 * 5 * 5) throw new Error('LZMA: bad properties');
  const lc = d % 9;
  d = (d - lc) / 9;
  const lp = d % 5;
  const pb = (d - lp) / 5;
  const posMask = (1 << pb) - 1;
  const lpMask = (1 << lp) - 1;

  const out = new Uint8Array(size);
  let outPos = 0;

  // the range decoder
  let pos = 1;
  let range = 0xffffffff;
  let code = 0;
  if (input.length < 5 || input[0] !== 0) throw new Error('LZMA: bad stream');
  for (let i = 0; i < 4; i++) code = ((code << 8) | input[pos++]) >>> 0;

  const probs = (count) => new Uint16Array(count).fill(BIT_MODEL_TOTAL / 2);
  const isMatch = probs(12 << 4);
  const isRep = probs(12);
  const isRepG0 = probs(12);
  const isRepG1 = probs(12);
  const isRepG2 = probs(12);
  const isRep0Long = probs(12 << 4);
  const posSlots = probs(4 << 6);
  const posDecoders = probs(1 + 128 - 14);
  const align = probs(16);
  const lenProbs = probs(514); // choice, choice2, low 16x8, mid 16x8, high 256
  const repLenProbs = probs(514);
  const literals = probs(0x300 << (lc + lp));

  function bit(table, index) {
    const p = table[index];
    const bound = (range >>> 11) * p;
    let result;
    if (code < bound) {
      range = bound;
      table[index] = p + ((BIT_MODEL_TOTAL - p) >> 5);
      result = 0;
    } else {
      range = (range - bound) >>> 0;
      code = (code - bound) >>> 0;
      table[index] = p - (p >> 5);
      result = 1;
    }
    if (range < TOP) {
      range = (range << 8) >>> 0;
      code = ((code << 8) | input[pos++]) >>> 0;
    }
    return result;
  }

  function direct(count) {
    let result = 0;
    for (; count > 0; count--) {
      range >>>= 1;
      let b = 0;
      if (code >= range) {
        code = (code - range) >>> 0;
        b = 1;
      }
      if (range < TOP) {
        range = (range << 8) >>> 0;
        code = ((code << 8) | input[pos++]) >>> 0;
      }
      result = ((result << 1) | b) >>> 0;
    }
    return result;
  }

  function tree(table, base, bits) {
    let m = 1;
    for (let i = 0; i < bits; i++) m = (m << 1) | bit(table, base + m);
    return m - (1 << bits);
  }

  function reverseTree(table, base, bits) {
    let m = 1;
    let symbol = 0;
    for (let i = 0; i < bits; i++) {
      const b = bit(table, base + m);
      m = (m << 1) | b;
      symbol |= b << i;
    }
    return symbol;
  }

  function length(table, posState) {
    if (bit(table, 0) === 0) return tree(table, 2 + (posState << 3), 3);
    if (bit(table, 1) === 0) return 8 + tree(table, 130 + (posState << 3), 3);
    return 16 + tree(table, 258, 8);
  }

  let state = 0;
  let rep0 = 0;
  let rep1 = 0;
  let rep2 = 0;
  let rep3 = 0;

  while (outPos < size) {
    if (pos > input.length + 4) throw new Error('LZMA: the stream ends early');
    const posState = outPos & posMask;
    if (bit(isMatch, (state << 4) + posState) === 0) {
      const previous = outPos > 0 ? out[outPos - 1] : 0;
      const base = 0x300 * (((outPos & lpMask) << lc) + (previous >> (8 - lc)));
      let symbol = 1;
      if (state >= 7) {
        let matchByte = out[outPos - rep0 - 1];
        do {
          const matchBit = (matchByte >> 7) & 1;
          matchByte <<= 1;
          const b = bit(literals, base + ((1 + matchBit) << 8) + symbol);
          symbol = (symbol << 1) | b;
          if (matchBit !== b) break;
        } while (symbol < 0x100);
      }
      while (symbol < 0x100) symbol = (symbol << 1) | bit(literals, base + symbol);
      out[outPos++] = symbol & 0xff;
      state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
      continue;
    }

    let len;
    if (bit(isRep, state) !== 0) {
      if (outPos === 0) throw new Error('LZMA: a repeat before any byte');
      if (bit(isRepG0, state) === 0) {
        if (bit(isRep0Long, (state << 4) + posState) === 0) {
          state = state < 7 ? 9 : 11;
          out[outPos] = out[outPos - rep0 - 1];
          outPos++;
          continue;
        }
      } else {
        let distance;
        if (bit(isRepG1, state) === 0) {
          distance = rep1;
        } else {
          if (bit(isRepG2, state) === 0) {
            distance = rep2;
          } else {
            distance = rep3;
            rep3 = rep2;
          }
          rep2 = rep1;
        }
        rep1 = rep0;
        rep0 = distance;
      }
      len = length(repLenProbs, posState);
      state = state < 7 ? 8 : 11;
    } else {
      rep3 = rep2;
      rep2 = rep1;
      rep1 = rep0;
      len = length(lenProbs, posState);
      state = state < 7 ? 7 : 10;
      const slot = tree(posSlots, (len > 3 ? 3 : len) << 6, 6);
      if (slot < 4) {
        rep0 = slot;
      } else {
        const directBits = (slot >> 1) - 1;
        let distance = ((2 | (slot & 1)) << directBits) >>> 0;
        if (slot < 14) {
          distance += reverseTree(posDecoders, distance - slot, directBits);
        } else {
          distance = (distance + ((direct(directBits - 4) << 4) >>> 0)) >>> 0;
          distance = (distance + reverseTree(align, 0, 4)) >>> 0;
        }
        rep0 = distance;
      }
      if (rep0 === 0xffffffff) break; // the end marker
    }

    len += 2;
    if (rep0 >= outPos) throw new Error('LZMA: a distance before the start');
    if (len > size - outPos) throw new Error('LZMA: more than its size');
    let from = outPos - rep0 - 1;
    for (; len > 0; len--) out[outPos++] = out[from++];
  }

  if (outPos !== size) throw new Error('LZMA: less than its size');
  return out;
}
