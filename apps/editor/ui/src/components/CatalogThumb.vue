<script setup lang="ts">
// One catalog entry's art (development_plan.md 7.3): its region of the image,
// with what the pixel analysis found drawn over it: the footprint band (the
// base it stands on), the anchor (where it stands) and named points (a door).
import { onMounted, ref, watch } from 'vue'

const props = defineProps<{
  src: string
  rect: [number, number, number, number] // x, y, w, h in image pixels
  base?: [number, number, number, number]
  anchor?: [number, number] // u, v across the region
  anchors?: Record<string, [number, number]>
  size: number // the canvas's longest side, CSS pixels
  marks?: boolean
}>()

const canvas = ref<HTMLCanvasElement | null>(null)
let image: HTMLImageElement | null = null

function draw(): void {
  const c = canvas.value
  if (!c || !image || !image.complete || image.naturalWidth === 0) return
  const [x, y, w, h] = props.rect
  if (w <= 0 || h <= 0) return
  const scale = props.size / Math.max(w, h)
  const cw = Math.max(1, Math.round(w * scale))
  const ch = Math.max(1, Math.round(h * scale))
  const ratio = window.devicePixelRatio || 1
  c.width = cw * ratio
  c.height = ch * ratio
  c.style.width = `${cw}px`
  c.style.height = `${ch}px`
  const g = c.getContext('2d')
  if (!g) return
  g.setTransform(ratio, 0, 0, ratio, 0, 0)
  g.imageSmoothingEnabled = scale < 1
  g.clearRect(0, 0, cw, ch)
  g.drawImage(image, x, y, w, h, 0, 0, cw, ch)
  if (!props.marks) return
  if (props.base && props.base[2] > 0) {
    const [bx, by, bw, bh] = props.base
    g.fillStyle = 'rgba(137, 180, 250, 0.35)'
    g.strokeStyle = '#89b4fa'
    g.lineWidth = 1
    g.fillRect((bx - x) * scale, (by - y) * scale, bw * scale, Math.max(2, bh * scale))
    g.strokeRect((bx - x) * scale + 0.5, (by - y) * scale + 0.5, bw * scale - 1, Math.max(2, bh * scale) - 1)
  }
  const dot = (u: number, v: number, color: string) => {
    g.fillStyle = color
    g.fillRect(u * cw - 3, v * ch - 3, 6, 6)
    g.strokeStyle = '#11111b'
    g.strokeRect(u * cw - 3.5, v * ch - 3.5, 7, 7)
  }
  if (props.anchor) dot(props.anchor[0], props.anchor[1], '#f38ba8')
  for (const point of Object.values(props.anchors ?? {})) dot(point[0], point[1], '#a6e3a1')
}

function load(): void {
  image = new Image()
  image.onload = draw
  image.src = props.src
}

onMounted(load)
watch(() => props.src, load)
watch(() => [props.rect, props.base, props.anchor, props.anchors, props.size, props.marks], draw, { deep: true })
</script>

<template>
  <canvas ref="canvas" class="thumb" />
</template>

<style scoped>
.thumb {
  display: block;
  background:
    repeating-conic-gradient(#2a2a3a 0% 25%, #232332 0% 50%) 0 0 / 12px 12px;
}
</style>
