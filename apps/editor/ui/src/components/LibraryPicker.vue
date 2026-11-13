<script setup lang="ts">
// A character (its sheet with its animations) or a prop's picture chosen from your library, for a drawing in
// AI Planner ▸ Build (Characters, Props & objects): copied into the project and used instead of drawing it.
import { computed, onMounted, ref } from 'vue'
import { onHostMessage, sendToHost } from '../ipc'

interface Item {
  id: string
  name: string
  path: string
  kind?: string
  clips?: string[]
  description?: string
  meta?: { roles?: string; tags?: string; description?: string; kind?: string }
}
const props = defineProps<{ kind: 'character' | 'prop'; title: string }>()
const emit = defineEmits<{ (e: 'pick', id: string): void; (e: 'close'): void }>()

const items = ref<Item[]>([])
const loaded = ref(false)
const filter = ref('')
onHostMessage((message) => {
  if (message.type === 'library:state') {
    items.value = (message.items as Item[]) ?? []
    loaded.value = true
  }
})
onMounted(() => sendToHost({ type: 'library:get' }))

const shown = computed(() => {
  const words = filter.value.trim().toLowerCase().split(/\s+/).filter(Boolean)
  return items.value
    .filter((i) => (props.kind === 'character' ? i.kind === 'character' : i.kind !== 'character'))
    .filter((i) => {
      const hay = [i.name, i.description, i.meta?.roles, i.meta?.tags, i.meta?.description, (i.clips ?? []).join(' ')]
        .filter(Boolean).join(' ').toLowerCase().replace(/[-_]/g, ' ')
      return words.every((w) => hay.includes(w))
    })
    .slice(0, 300)
})
</script>

<template>
  <div class="lightbox" @click.self="emit('close')">
    <div class="box">
      <div class="head">
        <b>{{ title }}</b>
        <input v-model="filter" placeholder="search" />
        <span class="spacer" />
        <button @click="emit('close')">Cancel</button>
      </div>
      <p v-if="!loaded" class="muted">Loading the library…</p>
      <p v-else-if="!shown.length" class="muted">
        {{ kind === 'character'
          ? 'No characters in your library match. Characters drawn in your projects are kept there (Library ▸ Characters) once their project is opened.'
          : 'No pictures in your library match.' }}
      </p>
      <div class="grid">
        <button v-for="i in shown" :key="i.id" class="pick" :title="i.description || i.name" @click="emit('pick', i.id)">
          <img :src="'https://aige-library.local/' + i.path" alt="" loading="lazy" :class="{ sheet: i.kind === 'character' }" />
          <span>{{ i.name }}</span>
          <span v-if="i.clips?.length" class="muted small">{{ i.clips.join(', ') }}</span>
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.lightbox {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 50;
}
.box {
  background: #1e1e2e;
  border: 1px solid #3a3a50;
  width: min(900px, 92vw);
  max-height: 85vh;
  display: flex;
  flex-direction: column;
  padding: 10px;
  gap: 8px;
}
.head {
  display: flex;
  align-items: center;
  gap: 8px;
}
.spacer {
  flex: 1;
}
.grid {
  overflow: auto;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(130px, 1fr));
  gap: 8px;
}
.pick {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  padding: 6px;
  background: #181825;
  border: 1px solid #313244;
  cursor: pointer;
  text-align: center;
}
.pick:hover {
  border-color: #89b4fa;
}
.pick img {
  width: 110px;
  height: 110px;
  object-fit: contain;
  image-rendering: pixelated;
}
.pick img.sheet {
  object-fit: cover;
  object-position: 0 0;
}
.muted {
  color: #8a8aa0;
}
.small {
  font-size: 0.8em;
}
</style>
