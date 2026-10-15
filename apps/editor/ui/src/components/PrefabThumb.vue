<script setup lang="ts">
// A prefab's sprite, cropped to its UV region (e.g. one tile of a tileset)
// and scaled to fit a square box. Textures are served from the open project
// (apps/editor/main.cpp maps it to aige-project.local).
import { computed } from 'vue'
import type { Prefab } from '../editorState'

const props = defineProps<{ prefab: Prefab; box: number }>()

const PROJECT_HOST = 'https://aige-project.local/'

const style = computed(() => {
  const p = props.prefab
  const [u0, v0, u1, v1] = p.uv
  const [w, h] = p.size
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
    transform: u0 > u1 ? 'scaleX(-1)' : undefined,
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
