<script setup>
defineProps({ data: Object })

// Isometric diamond helpers for the two little illustrations (a 6x6 grid of cells).
const W = 26, H = 13, OX = 160, OY = 30
const cell = (x, y) => {
  const cx = OX + (x - y) * W, cy = OY + (x + y) * H
  return `${cx},${cy} ${cx + W},${cy + H} ${cx},${cy + 2 * H} ${cx - W},${cy + H}`
}
const cells = []
for (let y = 0; y < 6; y++) for (let x = 0; x < 6; x++) cells.push({ x, y, k: `${x}-${y}` })
// A box (building or prop) standing on cell x,y, `h` tall.
const box = (x, y, h) => {
  const cx = OX + (x - y) * W, cy = OY + (x + y) * H
  return {
    top: `${cx},${cy - h} ${cx + W},${cy + H - h} ${cx},${cy + 2 * H - h} ${cx - W},${cy + H - h}`,
    left: `${cx - W},${cy + H - h} ${cx},${cy + 2 * H - h} ${cx},${cy + 2 * H} ${cx - W},${cy + H}`,
    right: `${cx},${cy + 2 * H - h} ${cx + W},${cy + H - h} ${cx + W},${cy + H} ${cx},${cy + 2 * H}`,
  }
}
// Back to front (by x + y), so nearer pieces cover farther ones.
const pieces = [[1, 1, 30], [4, 1, 22], [1, 4, 18], [2, 3, 10], [4, 4, 34]].map(([x, y, h]) => box(x, y, h))
const walk = new Set(['0-2', '1-2', '2-2', '3-2', '4-2', '5-2', '2-0', '2-1', '2-4', '2-5', '3-3', '3-4', '0-3', '5-3'])
</script>

<template>
  <section id="methods">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">Paint or Assembled</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>

      <div class="grid two">
        <article v-for="m in data.items" :key="m.key" class="card method reveal">
          <svg class="art" viewBox="0 0 320 200" role="img" :aria-label="`${m.name} method illustration`">
            <template v-if="m.key === 'assembled'">
              <polygon v-for="c in cells" :key="c.k" :points="cell(c.x, c.y)" class="ground" />
              <g v-for="(p, i) in pieces" :key="i" class="piece">
                <polygon :points="p.left" class="l" /><polygon :points="p.right" class="r" /><polygon :points="p.top" class="t" />
              </g>
            </template>
            <template v-else>
              <defs>
                <linearGradient id="paintfill" x1="0" y1="0" x2="1" y2="1">
                  <stop offset="0" stop-color="#3f6b3a" /><stop offset="0.5" stop-color="#7a6a3e" /><stop offset="1" stop-color="#2f5a55" />
                </linearGradient>
              </defs>
              <polygon :points="`${OX},${OY} ${OX + 6 * W},${OY + 6 * H} ${OX},${OY + 12 * H} ${OX - 6 * W},${OY + 6 * H}`" fill="url(#paintfill)" />
              <g v-for="(p, i) in pieces" :key="i" class="painted">
                <polygon :points="p.left" /><polygon :points="p.right" /><polygon :points="p.top" />
              </g>
              <polygon v-for="c in cells.filter((c) => walk.has(c.k))" :key="c.k" :points="cell(c.x, c.y)" class="walk" />
            </template>
          </svg>
          <span class="chip">{{ m.tagline }}</span>
          <h3>{{ m.name }}</h3>
          <p>{{ m.text }}</p>
          <dl>
            <div><dt>Best for</dt><dd>{{ m.bestFor }}</dd></div>
            <div><dt>Art</dt><dd>{{ m.art }}</dd></div>
            <div><dt>Steps</dt><dd class="mono">{{ m.steps }}</dd></div>
          </dl>
        </article>
      </div>
    </div>
  </section>
</template>

<style scoped>
.method { display: flex; flex-direction: column; gap: 12px; padding: 0 0 28px; }
.method > :not(.art) { margin-left: 28px; margin-right: 28px; }
.method .chip { align-self: flex-start; }
.method h3 { font-size: 28px; font-weight: 800; margin: 4px 28px 0; }
.art { width: 100%; height: auto; background: #0a1022; border-bottom: 1px solid var(--line); padding: 16px 0; }
.ground { fill: #1a2a1f; stroke: #2c4a33; stroke-width: 1; }
.piece .t { fill: #7cc4ff; } .piece .l { fill: #2f7cf6; } .piece .r { fill: #1d4ed8; }
.piece { stroke: #0b1020; stroke-width: 1; }
.painted polygon { fill: rgba(30, 20, 10, 0.35); stroke: rgba(255, 230, 180, 0.25); stroke-width: 1; }
.walk { fill: rgba(34, 211, 238, 0.22); stroke: rgba(34, 211, 238, 0.7); stroke-width: 1; stroke-dasharray: 3 2; }
dl { margin: 10px 28px 0; display: grid; gap: 10px; border-top: 1px solid var(--line); padding-top: 16px; }
dl div { display: grid; grid-template-columns: 90px minmax(0, 1fr); gap: 12px; font-size: 14px; }
dt { color: var(--dim); font: 500 12px/1.6 var(--mono); text-transform: uppercase; letter-spacing: 0.08em; }
dd { margin: 0; color: var(--pale); }
.mono { font-family: var(--mono); font-size: 13px; }
</style>
