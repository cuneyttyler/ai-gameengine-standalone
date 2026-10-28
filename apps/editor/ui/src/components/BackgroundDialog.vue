<script setup lang="ts">
// Edit ▸ Background Image… (docs/places/background.md): the background painted again, by a model, from words. With a
// build: that place's background, from its plan and its description (the scene laid with it again when its place is
// open). With none: the open scene's ground painted from the words alone, behind everything as one picture. Or a
// picture file of your own.
import { nextTick, ref, watch } from 'vue'
import { BACKGROUND_MODELS } from '../backgroundModels'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'

const backdrop = ref<HTMLElement | null>(null)
const builds = ref<{ id: string; name: string; stage: string }[]>([])
const build = ref('') // '' : none, the open scene
const model = ref('bfl:flux-3-image')
const text = ref('')
const loading = ref(false)

watch(
  () => editorState.showBackground,
  async (open) => {
    if (open) {
      loading.value = true
      sendToHost({ type: 'background:request' })
    }
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

onHostMessage((message) => {
  if (message.type !== 'background:state') return
  if (message.builds) builds.value = message.builds as typeof builds.value
  if (typeof message.build === 'string') build.value = message.build
  text.value = (message.description as string) ?? ''
  model.value = (message.model as string) || editorState.ai.backgroundProvider || 'bfl:flux-3-image'
  loading.value = false
})

function pickBuild(id: string): void {
  build.value = id
  loading.value = true
  sendToHost({ type: 'background:request', build: id })
}

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.showBackground = false
}

function regenerate(): void {
  sendToHost({ type: 'background:regenerate', build: build.value, model: model.value, description: text.value })
  close()
}

function fromFile(): void {
  sendToHost({ type: 'background:fromFile', build: build.value })
  close()
}
</script>

<template>
  <div v-if="editorState.showBackground" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <h3>Background image</h3>
      <p class="muted">
        Painted again by a model from the words below: a build's background from its plan, or, with no build, the open
        scene's ground behind everything.
      </p>
      <div class="grid">
        <label>Build</label>
        <select :value="build" @change="pickBuild(($event.target as HTMLSelectElement).value)">
          <option value="">None: the open scene</option>
          <option v-for="b in builds" :key="b.id" :value="b.id">{{ b.name || b.id }} <template v-if="b.stage">({{ b.stage }})</template></option>
        </select>
        <label>Model</label>
        <select v-model="model">
          <option v-for="m in BACKGROUND_MODELS" :key="m.id" :value="m.id">{{ m.label }}</option>
        </select>
        <label class="top">Words</label>
        <textarea
          v-model="text"
          rows="7"
          spellcheck="false"
          :disabled="loading"
          :placeholder="build ? 'What the place is and how it looks, then a line per kind of ground' : 'The ground: what it is and how it looks (a mossy forest floor with a dirt path, autumn leaves)'"
        />
      </div>
      <p class="muted">{{ build ? 'Its plan places each kind of ground; the words say how it looks.' : 'Painted in the project\'s view, to fit the scene.' }}</p>
      <div class="actions">
        <button @click="fromFile">Use a picture file…</button>
        <span class="grow" />
        <button @click="close">Cancel</button>
        <button class="primary" :disabled="loading || !text.trim() || editorState.ai.state !== 'ready'" :title="editorState.ai.state !== 'ready' ? 'Turn AI on to paint' : ''" @click="regenerate">
          Regenerate
        </button>
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
  width: min(620px, 92vw);
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
.grid select,
.grid textarea {
  width: 100%;
  box-sizing: border-box;
}
.grid textarea {
  resize: vertical;
  font: inherit;
}
.top {
  align-self: start;
  padding-top: 4px;
}
.actions {
  display: flex;
  gap: 8px;
  margin-top: 12px;
}
.grow {
  flex: 1;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: 4px 0;
}
</style>
