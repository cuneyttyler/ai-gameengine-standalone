<script setup lang="ts">
// A picture's thumbnail; an animation sheet's shows its first frame only (not every pose at once).
import { computed, onMounted } from 'vue'
import { requestAssetFrames, sheetFrames } from '../assetFrames'

const props = defineProps<{ path: string; src: string; size?: number }>()
onMounted(requestAssetFrames)
const box = computed(() => props.size ?? 56)
const sheet = computed(() => sheetFrames.value[props.path])
const style = computed<Record<string, string>>(() => {
  const s = sheet.value
  if (!s) return {}
  const [x, y, w, h] = s.frame
  const k = Math.min(box.value / w, box.value / h)
  return {
    width: `${w * k}px`,
    height: `${h * k}px`,
    backgroundImage: `url("${props.src}")`,
    backgroundPosition: `-${x * k}px -${y * k}px`,
    backgroundSize: `${s.width * k}px ${s.height * k}px`,
    imageRendering: w <= 128 ? 'pixelated' : 'auto',
  }
})
</script>

<template>
  <span v-if="sheet" class="frame" :style="style"></span>
  <img v-else :src="src" alt="" draggable="false" />
</template>

<style scoped>
.frame { display: inline-block; background-repeat: no-repeat; }
</style>
