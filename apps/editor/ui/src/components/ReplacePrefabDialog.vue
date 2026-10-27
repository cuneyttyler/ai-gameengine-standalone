<script setup lang="ts">
// Edit ▸ Replace prefab… (and a prefab's menu in the Assets panel): every copy of one prefab in
// the scene becomes another, each where it was, keeping how it was turned and mirrored. One
// undo step. The AI panel does the same with prefab.replace / terrain.replace.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

const backdrop = ref<HTMLElement | null>(null)
const from = ref('')
const to = ref('')
const orient = ref<'keep' | 'mirror' | 'none'>('keep')
// Opened for one thing: it alone, or every copy of its prefab.
const which = ref<'one' | 'all'>('all')

watch(
  () => editorState.replacePrefab,
  async (open) => {
    if (open !== null) {
      from.value = open
      to.value = ''
      orient.value = 'keep'
      which.value = editorState.replaceEntity !== null ? 'one' : 'all'
    }
    await nextTick()
    if (open !== null && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)
function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.replacePrefab = null
  editorState.replaceEntity = null
}
const fromPrefab = computed(() => editorState.prefabs.find((p) => p.path === from.value))
// Like for like: tiles for tiles, things for things.
const choices = computed(() => editorState.prefabs.filter((p) => p.path !== from.value && (!fromPrefab.value || p.isTile === fromPrefab.value.isTile)))
function replace(): void {
  if (!from.value || !to.value) return
  if (which.value === 'one' && editorState.replaceEntity !== null) sendToHost({ type: 'entity:replacePrefab', id: editorState.replaceEntity, prefab: to.value })
  else sendToHost({ type: 'scene:replacePrefab', from: from.value, to: to.value, orient: orient.value, tile: !!fromPrefab.value?.isTile })
  close()
}
</script>

<template>
  <div
    v-if="editorState.replacePrefab !== null"
    ref="backdrop"
    class="backdrop ed-form"
    tabindex="-1"
    @mousedown.self="close"
    @keydown.esc.stop="close"
  >
    <div class="sheet">
      <h3>Replace a prefab in the scene</h3>
      <p class="muted">Every copy of the first becomes the second, where it was. One undo step.</p>
      <div class="grid">
        <label>Replace</label>
        <select v-model="from">
          <option value="">a prefab…</option>
          <option v-for="p in editorState.prefabs" :key="p.path" :value="p.path">{{ p.path }}</option>
        </select>
        <label>With</label>
        <select v-model="to">
          <option value="">a prefab…</option>
          <option v-for="p in choices" :key="p.path" :value="p.path">{{ p.path }}</option>
        </select>
        <template v-if="editorState.replaceEntity !== null">
          <label>Which</label>
          <select v-model="which">
            <option value="one">this one</option>
            <option value="all">every copy in the scene</option>
          </select>
        </template>
        <template v-if="!fromPrefab?.isTile && which === 'all'">
          <label>Facing</label>
          <select v-model="orient">
            <option value="keep">as each copy faces (turned, mirrored)</option>
            <option value="mirror">each the other way</option>
            <option value="none">as the new prefab is</option>
          </select>
        </template>
      </div>
      <p v-if="fromPrefab?.isTile" class="muted">A ground tile: hand-painted tiles and a built place's ground alike.</p>
      <div class="actions">
        <button @click="close">Cancel</button>
        <button class="primary" :disabled="!from || !to" @click="replace">{{ which === 'one' ? 'Replace it' : 'Replace every copy' }}</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  z-index: 1000;
  background: rgba(5, 5, 10, 0.55);
  display: flex;
  align-items: center;
  justify-content: center;
  outline: none;
}
.sheet {
  width: min(560px, 92vw);
  background: #181825;
  border: 1px solid #333;
  border-top: 2px solid #3b82f6;
  padding: 14px 18px;
}
h3 {
  margin: 0 0 4px;
}
.grid {
  display: grid;
  grid-template-columns: 80px 1fr;
  gap: 8px 10px;
  align-items: center;
  margin: 10px 0;
}
.grid select {
  width: 100%;
}
.actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 12px;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: 4px 0;
}
</style>
