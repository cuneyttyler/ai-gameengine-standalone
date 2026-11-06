<script setup lang="ts">
// A prefab's sprite, cropped to its UV region (e.g. one tile of a tileset)
// and scaled to fit a square box. Textures are served from the open project
// (apps/editor/main.cpp maps it to aige-project.local).
import { computed, watchEffect } from 'vue'
import { derivedFrames, ensureFrame, sheetFrames } from '../assetFrames'
import type { Prefab } from '../editorState'

const props = defineProps<{ prefab: Prefab; box: number }>()

const PROJECT_HOST = 'https://aige-project.local/'

// A character's (or another animation sheet's) picture: its idle frame facing the camera, read from the sheet; the
// region saved in the prefab goes stale once the sheet grows (a walk added below the poses).
const sheet = computed(() => (props.prefab.texture ? sheetFrames.value[props.prefab.texture] ?? derivedFrames.value[props.prefab.texture] : undefined))
watchEffect(() => {
  const t = props.prefab.texture
  if (t && !sheetFrames.value[t] && !(t in derivedFrames.value)) ensureFrame(t)
})

const style = computed(() => {
  const p = props.prefab
  const s = sheet.value
  const [u0, v0, u1, v1] = s
    ? [s.frame[0] / s.width, s.frame[1] / s.height, (s.frame[0] + s.frame[2]) / s.width, (s.frame[1] + s.frame[3]) / s.height]
    : p.uv
  const [w, h] = s ? [s.frame[2], s.frame[3]] : p.size
  if (!p.texture || w <= 0 || h <= 0) {
    return null
  }
  const scale = Math.min(props.box / w, props.box / h)
  const width = w * scale
  const height = h * scale
  const spanU = Math.abs(u1 - u0) || 1
  const spanV = Math.abs(v1 - v0) || 1
  const backgroundWidth = width / spanU
  const backgroundHeight = height / spanV
  const url = PROJECT_HOST + p.texture.split('/').map(encodeURIComponent).join('/')
  return {
    width: `${width}px`,
    height: `${height}px`,
    backgroundImage: `url("${url}")`,
    backgroundSize: `${backgroundWidth}px ${backgroundHeight}px`,
    backgroundPosition: `${-Math.min(u0, u1) * backgroundWidth}px ${-Math.min(v0, v1) * backgroundHeight}px`,
    transform: (s ? p.uv[0] > p.uv[2] : u0 > u1) ? 'scaleX(-1)' : undefined,
  }
})
</script>

<template>
  <span class="thumb" :style="{ width: `${box}px`, height: `${box}px` }">
    <span v-if="style" class="image" :style="style"></span>
    <span v-else class="placeholder" :title="'Uses the built-in placeholder look'">{{ prefab.name.charAt(0) }}</span>
  </span>
</template>

<style scoped>
.thumb {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}
.image {
  display: block;
  background-repeat: no-repeat;
  image-rendering: pixelated;
}
.placeholder {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 70%;
  height: 70%;
  background: #313148;
  font-size: 0.9em;
  text-transform: uppercase;
  opacity: 0.8;
}
</style>
