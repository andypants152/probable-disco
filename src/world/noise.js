export function hash(x, z) {
  let n = x * 374761393 + z * 668265263;
  n = (n ^ (n >> 13)) * 1274126177;
  return ((n ^ (n >> 16)) >>> 0) / 4294967295;
}

export function smoothstep(value) {
  return value * value * (3 - 2 * value);
}

export function lerp(a, b, amount) {
  return a + (b - a) * amount;
}

export function noise2D(x, z) {
  const ix = Math.floor(x);
  const iz = Math.floor(z);
  const fx = smoothstep(x - ix);
  const fz = smoothstep(z - iz);
  const a = hash(ix, iz) * 2 - 1;
  const b = hash(ix + 1, iz) * 2 - 1;
  const c = hash(ix, iz + 1) * 2 - 1;
  const d = hash(ix + 1, iz + 1) * 2 - 1;

  return lerp(lerp(a, b, fx), lerp(c, d, fx), fz);
}

export function noise3D(x, y, z) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const iz = Math.floor(z);
  const fx = smoothstep(x - ix);
  const fy = smoothstep(y - iy);
  const fz = smoothstep(z - iz);

  function corner(offsetX, offsetY, offsetZ) {
    return hash(ix + offsetX + (iy + offsetY) * 57, iz + offsetZ - (iy + offsetY) * 131) * 2 - 1;
  }

  const x00 = lerp(corner(0, 0, 0), corner(1, 0, 0), fx);
  const x10 = lerp(corner(0, 1, 0), corner(1, 1, 0), fx);
  const x01 = lerp(corner(0, 0, 1), corner(1, 0, 1), fx);
  const x11 = lerp(corner(0, 1, 1), corner(1, 1, 1), fx);

  return lerp(lerp(x00, x10, fy), lerp(x01, x11, fy), fz);
}

export function fbm(x, z, octaves = 4) {
  let value = 0;
  let amplitude = 0.5;
  let frequency = 1;
  let totalAmplitude = 0;

  // Layered noise breaks up repeated per-cell thresholds while staying deterministic.
  for (let octave = 0; octave < octaves; octave += 1) {
    value += noise2D(x * frequency, z * frequency) * amplitude;
    totalAmplitude += amplitude;
    amplitude *= 0.5;
    frequency *= 2;
  }

  return value / totalAmplitude;
}

export function randomRange(min, max) {
  return min + Math.random() * (max - min);
}

export function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

export function treeRand(originX, originZ, salt) {
  return hash(originX + salt * 101, originZ - salt * 137);
}

export function choose(value, options) {
  return options[Math.floor(value * options.length) % options.length];
}

export function pixelJitter(seed, salt) {
  return (treeRand(seed * 4096, seed * 8192, salt) - 0.5) * 0.035;
}
