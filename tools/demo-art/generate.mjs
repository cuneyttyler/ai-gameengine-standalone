// Generates the demo project's placeholder pixel art and sound effects.
//
//   node tools/demo-art/generate.mjs <outDir>
//
// Writes textures/*.png and audio/*.wav under <outDir>. Pointing it at a
// project's assets folder (e.g. projects/demo/assets) is the same layout the
// editor's Import button produces; the asset pipeline picks them up and
// writes .meta files on the next scan. tools/make_demo_level then builds the
// demo scene from them.
//
// Everything is drawn procedurally with a fixed random seed, so the output is
// identical on every run and the art can be tweaked by editing this file.
// No dependencies beyond Node's standard library.

import { deflateSync } from 'node:zlib'
import { mkdirSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'

const outDir = process.argv[2]
if (!outDir) {
  console.error('usage: node tools/demo-art/generate.mjs <outDir>   (e.g. projects/demo/assets)')
  process.exit(1)
}

// ---------------------------------------------------------------- utilities

function mulberry32(seed) {
  return () => {
    seed |= 0
    seed = (seed + 0x6d2b79f5) | 0
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}

function hex(color, alpha = 255) {
  const value = parseInt(color.slice(1), 16)
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255, alpha]
}

class Canvas {
  constructor(width, height) {
    this.width = width
    this.height = height
    this.pixels = new Uint8Array(width * height * 4)
  }
  set(x, y, color) {
    x = Math.floor(x)
    y = Math.floor(y)
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) return
    const i = (y * this.width + x) * 4
    const [r, g, b, a] = color
    if (a === 255 || this.pixels[i + 3] === 0) {
      this.pixels.set([r, g, b, a], i)
      return
    }
    // Alpha-blend over what's there (shadows, highlights).
    const t = a / 255
    this.pixels[i] = Math.round(r * t + this.pixels[i] * (1 - t))
    this.pixels[i + 1] = Math.round(g * t + this.pixels[i + 1] * (1 - t))
    this.pixels[i + 2] = Math.round(b * t + this.pixels[i + 2] * (1 - t))
    this.pixels[i + 3] = Math.min(255, this.pixels[i + 3] + a)
  }
  alpha(x, y) {
    if (x < 0 || y < 0 || x >= this.width || y >= this.height) return 0
    return this.pixels[(y * this.width + x) * 4 + 3]
  }
  rect(x0, y0, w, h, color) {
    for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) this.set(x, y, color)
  }
  ellipse(cx, cy, rx, ry, color) {
    for (let y = Math.floor(cy - ry); y <= Math.ceil(cy + ry); y++) {
      for (let x = Math.floor(cx - rx); x <= Math.ceil(cx + rx); x++) {
        const dx = (x + 0.5 - cx) / rx
        const dy = (y + 0.5 - cy) / ry
        if (dx * dx + dy * dy <= 1) this.set(x, y, color)
      }
    }
  }
  // 1px dark outline around everything opaque — the classic sprite look.
  outline(color) {
    const add = []
    for (let y = 0; y < this.height; y++) {
      for (let x = 0; x < this.width; x++) {
        if (this.alpha(x, y) > 128) continue
        if ([[1, 0], [-1, 0], [0, 1], [0, -1]].some(([dx, dy]) => this.alpha(x + dx, y + dy) > 200)) {
          add.push([x, y])
        }
      }
    }
    for (const [x, y] of add) this.set(x, y, color)
  }
  // Copies another canvas in at (ox, oy).
  blit(source, ox, oy) {
    for (let y = 0; y < source.height; y++) {
      for (let x = 0; x < source.width; x++) {
        const i = (y * source.width + x) * 4
        if (source.pixels[i + 3] > 0) this.set(ox + x, oy + y, Array.from(source.pixels.slice(i, i + 4)))
      }
    }
  }
}

const crcTable = new Uint32Array(256).map((_, n) => {
  let c = n
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
  return c >>> 0
})

function crc32(bytes) {
  let crc = 0xffffffff
  for (const byte of bytes) crc = crcTable[(crc ^ byte) & 255] ^ (crc >>> 8)
  return (crc ^ 0xffffffff) >>> 0
}

function encodePng(canvas) {
  const chunk = (type, data) => {
    const out = Buffer.alloc(12 + data.length)
    out.writeUInt32BE(data.length, 0)
    out.write(type, 4, 'ascii')
    data.copy(out, 8)
    out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length)
    return out
  }
  const header = Buffer.alloc(13)
  header.writeUInt32BE(canvas.width, 0)
  header.writeUInt32BE(canvas.height, 4)
  header.set([8, 6, 0, 0, 0], 8) // 8-bit RGBA
  const raw = Buffer.alloc((canvas.width * 4 + 1) * canvas.height)
  for (let y = 0; y < canvas.height; y++) {
    raw[y * (canvas.width * 4 + 1)] = 0 // filter: none
    Buffer.from(canvas.pixels.buffer, y * canvas.width * 4, canvas.width * 4).copy(raw, y * (canvas.width * 4 + 1) + 1)
  }
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw)),
    chunk('IEND', Buffer.alloc(0)),
  ])
}

function encodeWav(samples, sampleRate = 22050) {
  const data = Buffer.alloc(samples.length * 2)
  samples.forEach((s, i) => data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, s)) * 32767), i * 2))
  const header = Buffer.alloc(44)
  header.write('RIFF', 0)
  header.writeUInt32LE(36 + data.length, 4)
  header.write('WAVEfmt ', 8)
  header.writeUInt32LE(16, 16)
  header.writeUInt16LE(1, 20) // PCM
  header.writeUInt16LE(1, 22) // mono
  header.writeUInt32LE(sampleRate, 24)
  header.writeUInt32LE(sampleRate * 2, 28)
  header.writeUInt16LE(2, 32)
  header.writeUInt16LE(16, 34)
  header.write('data', 36)
  header.writeUInt32LE(data.length, 40)
  return Buffer.concat([header, data])
}

// ------------------------------------------------------------------ palette

const OUTLINE = hex('#1c1b22')
const SHADOW = [0, 0, 0, 70]

// ------------------------------------------------------------------- ground

// Ground tiles are 64x64 cells: the 64x32 iso diamond sits in the bottom
// half, leaving room above for tall tiles (walls, hedges) in the same
// tileset. The engine anchors taller tile sprites so their bottom diamond
// stays on the cell.
const TILE = 64

function inDiamond(x, y, cx, cy, rx, ry) {
  return Math.abs(x + 0.5 - cx) / rx + Math.abs(y + 0.5 - cy) / ry <= 1
}

function groundTile(base, light, dark, decorate, seed) {
  const rand = mulberry32(seed)
  const c = new Canvas(TILE, TILE)
  for (let y = 32; y < 64; y++) {
    for (let x = 0; x < 64; x++) {
      if (!inDiamond(x, y, 32, 48, 32, 16)) continue
      const edge = !inDiamond(x, y, 32, 48, 30.5, 15)
      const n = rand()
      let color = n < 0.12 ? light : n > 0.9 ? dark : base
      if (edge) color = dark
      c.set(x, y, hex(color))
    }
  }
  decorate?.(c, rand)
  return c
}

function sprinkle(c, rand, count, colors, size = 1) {
  for (let i = 0; i < count; i++) {
    const x = Math.floor(rand() * 64)
    const y = 32 + Math.floor(rand() * 32)
    if (!inDiamond(x, y, 32, 48, 26, 12)) continue
    const color = hex(colors[Math.floor(rand() * colors.length)])
    c.rect(x, y, size, size, color)
  }
}

const grass = groundTile('#5a8f3c', '#6fa84a', '#477333', (c, r) => sprinkle(c, r, 40, ['#79b552', '#4c7a35']), 1)
const flowers = groundTile('#5a8f3c', '#6fa84a', '#477333', (c, r) => {
  sprinkle(c, r, 30, ['#79b552'])
  sprinkle(c, r, 14, ['#e8d36a', '#d97b9a', '#f2f2f2'], 2)
}, 2)
const dirt = groundTile('#8a6a45', '#a07f55', '#6e5236', (c, r) => sprinkle(c, r, 18, ['#b29a78', '#5e4630'], 2), 3)
const stone = groundTile('#8c8c94', '#a3a3ab', '#6f6f78', (c) => {
  // Flagstone seams along the diamond's axes.
  for (let t = -1; t <= 1; t += 2) {
    for (let i = -14; i <= 14; i++) {
      for (const [x, y] of [
        [32 + i * 2 + t, 48 + i - t * 0.5],
        [32 + i * 2 - t, 48 - i - t * 0.5],
      ]) {
        if (inDiamond(Math.floor(x), Math.floor(y), 32, 48, 30, 14.5)) c.set(x, y, hex('#5c5c66'))
      }
    }
  }
}, 4)
const water = groundTile('#2f5d8a', '#3b6fa0', '#264c72', (c, r) => {
  for (let i = 0; i < 9; i++) {
    const x = 14 + Math.floor(r() * 36)
    const y = 40 + Math.floor(r() * 16)
    c.rect(x, y, 4, 1, hex('#8fc0e0', 200))
  }
}, 5)

// A solid block: lit top face plus two shaded side faces, all 64x64.
function block(top, topLight, left, right, mortar, decorate, seed) {
  const rand = mulberry32(seed)
  const c = new Canvas(TILE, TILE)
  for (let y = 0; y < 64; y++) {
    for (let x = 0; x < 64; x++) {
      const topLow = 16 + 16 * (1 - Math.abs(x + 0.5 - 32) / 32) // lower edge of the top diamond
      if (inDiamond(x, y, 32, 16, 32, 16)) {
        c.set(x, y, hex(rand() < 0.15 ? topLight : top))
      } else if (y > topLow && y <= topLow + 32) {
        c.set(x, y, hex(x < 32 ? left : right))
      }
    }
  }
  decorate?.(c, rand, mortar)
  // Edge highlight along the top face's front edges.
  for (let x = 0; x < 64; x++) c.set(x, 16 + 16 * (1 - Math.abs(x + 0.5 - 32) / 32), hex(topLight))
  return c
}

function bricks(c, rand, mortar) {
  for (let row = 0; row < 4; row++) {
    for (let x = 0; x < 64; x++) {
      const topLow = 16 + 16 * (1 - Math.abs(x + 0.5 - 32) / 32)
      const y = Math.floor(topLow + 8 * (row + 1))
      if (row < 3) c.set(x, y, hex(mortar))
      const offset = row % 2 ? 6 : 0
      if ((x + offset) % 12 === 0 && x !== 32) {
        for (let k = 1; k < 8; k++) c.set(x, Math.floor(topLow + 8 * row + k), hex(mortar))
      }
    }
  }
}

const wall = block('#9a958c', '#b0aa9f', '#6e6a63', '#85807a', '#55524d', bricks, 6)
const mossyWall = block('#8f9a7c', '#a7b28f', '#666b58', '#7b8268', '#4f5446', (c, r, m) => {
  bricks(c, r, m)
  for (let i = 0; i < 60; i++) {
    const x = Math.floor(r() * 64)
    const y = 20 + Math.floor(r() * 40)
    if (c.alpha(x, y)) c.rect(x, y, 2, 1, hex(r() < 0.5 ? '#5d7d3c' : '#4a6a2e'))
  }
}, 7)
const hedge = block('#3f6e30', '#4f8a3a', '#2f5524', '#37602a', '#2a4a20', (c, r) => {
  for (let i = 0; i < 140; i++) {
    const x = Math.floor(r() * 64)
    const y = Math.floor(r() * 64)
    if (c.alpha(x, y)) c.set(x, y, hex(r() < 0.5 ? '#5a9a44' : '#28461e'))
  }
}, 8)

// Tileset: 4 columns x 2 rows of 64x64 cells.
//   0 grass   1 grass+flowers   2 dirt path   3 stone floor
//   4 wall    5 mossy wall      6 water       7 hedge
const tileset = new Canvas(TILE * 4, TILE * 2)
;[grass, flowers, dirt, stone, wall, mossyWall, water, hedge].forEach((tile, i) =>
  tileset.blit(tile, (i % 4) * TILE, Math.floor(i / 4) * TILE),
)

// --------------------------------------------------------------- characters

// 32x48, facing right; the bottom few pixels hold a soft shadow so the
// character reads as standing on the ground.
function character({ skin, hair, body, bodyDark, legs, boots, accessory }) {
  const c = new Canvas(32, 48)
  c.ellipse(16, 45, 9, 2.5, SHADOW)
  // legs + boots
  c.rect(12, 34, 3, 8, hex(legs))
  c.rect(17, 34, 3, 8, hex(legs))
  c.rect(11, 41, 5, 3, hex(boots))
  c.rect(17, 41, 5, 3, hex(boots))
  // torso
  c.rect(10, 22, 12, 13, hex(body))
  c.rect(10, 31, 12, 2, hex(bodyDark)) // belt
  c.rect(10, 22, 2, 13, hex(bodyDark)) // shading on the far side
  // arms
  c.rect(8, 23, 2, 9, hex(body))
  c.rect(22, 23, 2, 9, hex(body))
  c.rect(8, 31, 2, 2, hex(skin))
  c.rect(22, 31, 2, 2, hex(skin))
  // head
  c.rect(11, 11, 10, 10, hex(skin))
  c.rect(18, 15, 1, 2, hex('#1c1b22')) // eye, facing right
  c.rect(11, 9, 10, 4, hex(hair))
  c.rect(11, 9, 3, 8, hex(hair))
  accessory?.(c)
  c.outline(OUTLINE)
  return c
}

const hero = character({
  skin: '#f0c8a0',
  hair: '#b8bcc8', // helmet
  body: '#3a5fc8',
  bodyDark: '#2a4596',
  legs: '#5b5f6e',
  boots: '#4a3424',
  accessory: (c) => {
    c.rect(11, 8, 10, 2, hex('#d6dae4')) // helmet rim
    c.rect(15, 5, 2, 4, hex('#c83a3a')) // plume
    // sword held forward
    c.rect(24, 22, 2, 12, hex('#d8dce6'))
    c.rect(24, 22, 1, 12, hex('#ffffff'))
    c.rect(22, 33, 6, 2, hex('#8a6a2e'))
    c.rect(24, 35, 2, 3, hex('#5a4020'))
  },
})

const skeleton = character({
  skin: '#e6e2d3',
  hair: '#e6e2d3',
  body: '#d4cfbd',
  bodyDark: '#a8a290',
  legs: '#c9c4b2',
  boots: '#8f8a78',
  accessory: (c) => {
    // eye sockets and a jaw line instead of a face
    c.rect(14, 14, 2, 2, hex('#2a2430'))
    c.rect(18, 14, 2, 2, hex('#2a2430'))
    c.rect(14, 18, 6, 1, hex('#8f8a78'))
    // ribs
    for (let y = 24; y < 31; y += 2) c.rect(12, y, 8, 1, hex('#8f8a78'))
    // rusty blade
    c.rect(24, 21, 2, 12, hex('#9c7650'))
    c.rect(22, 32, 6, 2, hex('#5e4630'))
  },
})

const villager = character({
  skin: '#e8b890',
  hair: '#5a3a22',
  body: '#8a6a45',
  bodyDark: '#6a4f32',
  legs: '#4f5a3a',
  boots: '#3a2a1a',
})

// ------------------------------------------------------------------- props

function potion() {
  const c = new Canvas(16, 20)
  c.ellipse(8, 18, 5, 1.5, SHADOW)
  c.ellipse(8, 12, 5, 5, hex('#d83a3a'))
  c.ellipse(7, 11, 3, 3, hex('#ef5a5a'))
  c.rect(6, 3, 4, 5, hex('#c8dce6', 200))
  c.rect(6, 2, 4, 2, hex('#8a6a45'))
  c.rect(5, 10, 1, 2, hex('#ffffff'))
  c.outline(OUTLINE)
  return c
}

function tree(seed) {
  const rand = mulberry32(seed)
  const c = new Canvas(64, 96)
  c.ellipse(32, 91, 18, 4, SHADOW)
  c.rect(28, 58, 8, 33, hex('#6a4a2e'))
  c.rect(28, 58, 3, 33, hex('#553a22'))
  c.rect(34, 58, 2, 33, hex('#82603e'))
  const blobs = [
    [32, 44, 22, 18, '#3c6e2c'],
    [22, 36, 15, 13, '#457a32'],
    [42, 34, 15, 13, '#457a32'],
    [32, 22, 17, 14, '#4f8a3a'],
    [26, 18, 9, 7, '#62a046'],
  ]
  for (const [x, y, rx, ry, color] of blobs) c.ellipse(x, y, rx, ry, hex(color))
  for (let i = 0; i < 70; i++) {
    const x = 12 + Math.floor(rand() * 40)
    const y = 8 + Math.floor(rand() * 50)
    if (c.alpha(x, y) && y < 58) c.set(x, y, hex(rand() < 0.5 ? '#6fb050' : '#2f5a22'))
  }
  c.outline(OUTLINE)
  return c
}

function rock() {
  const c = new Canvas(32, 24)
  c.ellipse(16, 21, 13, 3, SHADOW)
  c.ellipse(16, 13, 13, 9, hex('#7d7c84'))
  c.ellipse(13, 10, 8, 5, hex('#9a99a1'))
  c.ellipse(20, 17, 8, 4, hex('#65646c'))
  c.rect(10, 8, 3, 1, hex('#b8b7bf'))
  c.outline(OUTLINE)
  return c
}

function crate() {
  const c = new Canvas(32, 32)
  c.ellipse(16, 30, 13, 2.5, SHADOW)
  c.rect(4, 6, 24, 23, hex('#a0763e'))
  c.rect(4, 6, 24, 3, hex('#b88a4a'))
  c.rect(4, 6, 3, 23, hex('#8a6232'))
  c.rect(25, 6, 3, 23, hex('#8a6232'))
  c.rect(4, 26, 24, 3, hex('#8a6232'))
  for (let i = 0; i < 18; i++) {
    c.rect(7 + i, 9 + i, 2, 1, hex('#7a5428'))
  }
  c.outline(OUTLINE)
  return c
}

// -------------------------------------------------------------------- audio

const RATE = 22050

function synth(seconds, fn) {
  const samples = new Float32Array(Math.floor(seconds * RATE))
  for (let i = 0; i < samples.length; i++) samples[i] = fn(i / RATE, i / samples.length)
  return samples
}

const noise = mulberry32(99)
const sounds = {
  swing: synth(0.16, (t, p) => (noise() * 2 - 1) * 0.35 * Math.sin(Math.PI * p) * (1 - p)),
  hit: synth(0.14, (t, p) => {
    const thump = Math.sin(2 * Math.PI * (140 - 80 * p) * t) * (1 - p) ** 2
    const click = p < 0.08 ? (noise() * 2 - 1) * (1 - p / 0.08) : 0
    return 0.7 * thump + 0.3 * click
  }),
  hurt: synth(0.22, (t, p) => (Math.sin(2 * Math.PI * (300 - 140 * p) * t) > 0 ? 0.25 : -0.25) * (1 - p)),
  death: synth(0.45, (t, p) => {
    const tone = Math.sin(2 * Math.PI * (260 - 200 * p) * t) * 0.4
    return (tone + (noise() * 2 - 1) * 0.2) * (1 - p)
  }),
  pickup: synth(0.28, (t, p) => {
    const freq = p < 0.33 ? 660 : p < 0.66 ? 880 : 1320
    return Math.sin(2 * Math.PI * freq * t) * 0.35 * (1 - p * 0.6)
  }),
}

// -------------------------------------------------------------------- write

mkdirSync(join(outDir, 'textures'), { recursive: true })
mkdirSync(join(outDir, 'audio'), { recursive: true })

const images = {
  'textures/ground.png': tileset,
  'textures/hero.png': hero,
  'textures/skeleton.png': skeleton,
  'textures/villager.png': villager,
  'textures/potion.png': potion(),
  'textures/tree.png': tree(11),
  'textures/rock.png': rock(),
  'textures/crate.png': crate(),
}
for (const [name, canvas] of Object.entries(images)) writeFileSync(join(outDir, name), encodePng(canvas))
for (const [name, samples] of Object.entries(sounds)) writeFileSync(join(outDir, 'audio', `${name}.wav`), encodeWav(samples))

console.log(`Wrote ${Object.keys(images).length} images and ${Object.keys(sounds).length} sounds to ${outDir}`)
