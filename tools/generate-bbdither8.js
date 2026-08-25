#!/usr/bin/env node
// SPDX-License-Identifier: GPL-2.0-only
/* Deterministic offline search for experimental 8x8 RGB565 rank maps. */

const initialMap = [
   0,58,15,53, 3,57,12,54, 47,21,32,26,44,22,35,25,
  48,10,63, 5,51, 9,60, 6, 31,37,16,42,28,38,19,41,
   1,59,14,52, 2,56,13,55, 46,20,33,27,45,23,34,24,
  49,11,62, 4,50, 8,61, 7, 30,36,17,43,29,39,18,40,
];

let randomState = 0x75086475;
function random() {
  randomState ^= randomState << 13;
  randomState ^= randomState >>> 17;
  randomState ^= randomState << 5;
  return (randomState >>> 0) / 0x100000000;
}

function spatialWeights() {
  const weights = Array.from({length: 64}, () => new Float64Array(64));
  for (let a = 0; a < 64; a++) for (let b = a + 1; b < 64; b++) {
    let dx = Math.abs((a & 7) - (b & 7));
    let dy = Math.abs((a >> 3) - (b >> 3));
    dx = Math.min(dx, 8 - dx); dy = Math.min(dy, 8 - dy);
    const d2 = dx * dx + dy * dy;
    let w = Math.exp(-0.72 * d2);
    if (!dx || !dy) w *= 1.18;
    weights[a][b] = weights[b][a] = w;
  }
  return weights;
}

function scatterCosts() {
  const costs = Array.from({length: 64}, () => new Float64Array(64));
  for (let a = 0; a < 64; a++) for (let b = 0; b < 64; b++) {
    const high = Math.max(a, b), low = Math.min(a, b);
    const darkTogether = high < 32 ? 32 - high : 0;
    const lightTogether = low >= 32 ? low - 31 : 0;
    costs[a][b] = darkTogether + lightTogether;
  }
  return costs;
}

function rgb565Costs() {
  const errors = Array.from({length: 64}, () => []);
  let samples = 0;
  const expand5 = q => Math.round(q * 255 / 31);
  const expand6 = q => Math.round(q * 255 / 63);
  for (let r = 0; r < 256; r += 17)
    for (let g = 0; g < 256; g += 17)
      for (let b = 0; b < 256; b += 17) {
        const inputY = (54 * r + 183 * g + 19 * b) / 256;
        if (inputY < 80 || inputY > 120) continue;
        const sr = r * 31, sg = g * 63, sb = b * 31;
        const qr = Math.floor(sr / 255), qg = Math.floor(sg / 255);
        const qb = Math.floor(sb / 255);
        const nr = Math.round((sr % 255) * 64 / 255);
        const ng = Math.round((sg % 255) * 64 / 255);
        const nb = Math.round((sb % 255) * 64 / 255);
        const perRank = [];
        for (let rank = 0; rank < 64; rank++) {
          const er = expand5(qr + (rank < nr)) - r;
          const eg = expand6(qg + (rank < ng)) - g;
          const eb = expand5(qb + (rank < nb)) - b;
          const ey = (54 * er + 183 * eg + 19 * eb) / 256;
          const erg = er - eg, ebg = eb - eg;
          perRank.push([ey, erg, ebg]);
        }
        for (let rank = 0; rank < 64; rank++) errors[rank].push(perRank[rank]);
        samples++;
      }
  const costs = Array.from({length: 64}, () => new Float64Array(64));
  for (let a = 0; a < 64; a++) for (let b = a; b < 64; b++) {
    let sum = 0;
    for (let s = 0; s < samples; s++) {
      const ea = errors[a][s], eb = errors[b][s];
      sum += ea[0] * eb[0] + 0.18 * ea[1] * eb[1] + 0.12 * ea[2] * eb[2];
    }
    costs[a][b] = costs[b][a] = sum / samples;
  }
  return costs;
}

const spatial = spatialWeights();
function score(map, costs) {
  let total = 0;
  for (let a = 0; a < 64; a++) for (let b = a + 1; b < 64; b++)
    total += spatial[a][b] * costs[map[a]][map[b]];
  return total;
}

function swapDelta(map, i, j, costs) {
  const a = map[i], b = map[j];
  let delta = 0;
  for (let k = 0; k < 64; k++) if (k !== i && k !== j) {
    const c = map[k];
    delta += spatial[i][k] * (costs[b][c] - costs[a][c]);
    delta += spatial[j][k] * (costs[a][c] - costs[b][c]);
  }
  return delta;
}

function search(costs, seed) {
  randomState = seed;
  const map = initialMap.slice();
  let current = score(map, costs), best = current, bestMap = map.slice();
  let deltaScale = 0;
  for (let n = 0; n < 512; n++) {
    const i = Math.floor(random() * 64), j = Math.floor(random() * 64);
    deltaScale += Math.abs(swapDelta(map, i, j, costs));
  }
  deltaScale = deltaScale / 512 || 1;
  for (let n = 0; n < 350000; n++) {
    const i = Math.floor(random() * 64), j = Math.floor(random() * 64);
    if (i === j) continue;
    const delta = swapDelta(map, i, j, costs);
    const temperature = deltaScale * Math.exp(-9 * n / 350000);
    if (delta < 0 || random() < Math.exp(-delta / temperature)) {
      [map[i], map[j]] = [map[j], map[i]];
      current += delta;
      if (current < best) { best = current; bestMap = map.slice(); }
    }
  }
  return {map: bestMap, score: best};
}

function print(name, result, baseline, costs) {
  console.log(`${name}: ${result.score.toFixed(3)} (initial ${score(baseline, costs).toFixed(3)})`);
  for (let y = 0; y < 8; y++)
    console.log(result.map.slice(y * 8, y * 8 + 8).map(v => String(v).padStart(2)).join(','));
}

const scatter = scatterCosts(), colour = rgb565Costs();
function normalise(costs) {
  let mean = 0, count = 0;
  for (let a = 0; a < 64; a++) for (let b = a + 1; b < 64; b++) {
    mean += costs[a][b]; count++;
  }
  mean /= count;
  let variance = 0;
  for (let a = 0; a < 64; a++) for (let b = a + 1; b < 64; b++)
    variance += (costs[a][b] - mean) ** 2;
  const scale = Math.sqrt(variance / count) || 1;
  return costs.map(row => Float64Array.from(row, value =>
    (value - mean) / scale));
}
const scatterN = normalise(scatter), colourN = normalise(colour);
const combinedObjective = Array.from({length: 64}, (_, a) =>
  Float64Array.from({length: 64}, (_, b) =>
    0.5 * scatterN[a][b] + 0.5 * colourN[a][b]));
print('bbdither8', search(combinedObjective, 0x48b21d50), initialMap,
  combinedObjective);
