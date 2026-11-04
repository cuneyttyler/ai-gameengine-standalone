<script setup lang="ts">
// The script editor: a project script (scripts/*.lua) read, edited and saved in the editor, with line numbers, Tab
// indenting and Ctrl+S; or opened in the program the computer uses for .lua files.
import { computed, nextTick, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { confirmDialog } from '../dialogs'
import { lookups } from '../lookups'

const backdrop = ref<HTMLElement | null>(null)
const area = ref<HTMLTextAreaElement | null>(null)
const gutter = ref<HTMLElement | null>(null)
const text = ref('')
const saved = ref('')
const loading = ref(false)
const error = ref('')
const status = ref('')
const dirty = computed(() => text.value !== saved.value)
const lines = computed(() => Math.max(1, text.value.split('\n').length))
const users = computed(() => (editorState.scriptEditor ? lookups.scriptUsers[editorState.scriptEditor] ?? [] : []))

watch(
  () => editorState.scriptEditor,
  async (path) => {
    text.value = saved.value = ''
    error.value = status.value = ''
    if (!path) return
    loading.value = true
    sendToHost({ type: 'script:read', path })
    await nextTick()
    if (backdrop.value) trackOverlay(backdrop.value)
  },
)

onHostMessage((message) => {
  if (message.type === 'script:text' && message.path === editorState.scriptEditor) {
    loading.value = false
    error.value = (message.error as string) ?? ''
    const body = (message.text as string) ?? ''
    text.value = saved.value = body.replace(/\r\n/g, '\n')
    if (!message.exists && !message.error) status.value = 'A new script: Save writes it.'
    nextTick(() => area.value?.focus())
  }
  if (message.type === 'script:saved' && message.path === editorState.scriptEditor) {
    status.value = message.ok ? 'Saved.' : 'Not saved.'
  }
})

function save(): void {
  if (!editorState.scriptEditor) return
  sendToHost({ type: 'script:save', path: editorState.scriptEditor, text: text.value })
  saved.value = text.value
}
async function close(): Promise<void> {
  if (dirty.value && !(await confirmDialog({ message: 'Close without saving your changes?', confirmLabel: 'Close', danger: true }))) return
  if (backdrop.value) untrackOverlay(backdrop.value)
  editorState.scriptEditor = null
}
function onKey(event: KeyboardEvent): void {
  if ((event.ctrlKey || event.metaKey) && event.key === 's') {
    event.preventDefault()
    save()
  } else if (event.key === 'Tab') {
    // Indent with spaces, as Lua is usually written here.
    event.preventDefault()
    const el = area.value!
    const start = el.selectionStart, end = el.selectionEnd
    text.value = text.value.slice(0, start) + '    ' + text.value.slice(end)
    nextTick(() => (el.selectionStart = el.selectionEnd = start + 4))
  }
}
function syncScroll(): void {
  if (gutter.value && area.value) gutter.value.scrollTop = area.value.scrollTop
}
</script>

<template>
  <div v-if="editorState.scriptEditor" ref="backdrop" class="backdrop ed-form" tabindex="-1" @mousedown.self="close" @keydown.esc.stop="close">
    <div class="sheet">
      <div class="head">
        <h3>{{ editorState.scriptEditor }}<span v-if="dirty" class="dot" title="Unsaved changes">●</span></h3>
        <span class="muted small grow">{{ users.length ? `Used by ${users.join(', ')}` : 'Not used by anything in this scene or by a package' }}</span>
        <button title="Open it in the program your computer uses for .lua files" @click="sendToHost({ type: 'script:open', path: editorState.scriptEditor })">Open outside</button>
      </div>
      <p v-if="error" class="error">{{ error }}</p>
      <p v-else-if="loading" class="muted">Loading…</p>
      <div v-else class="code">
        <div ref="gutter" class="gutter"><div v-for="n in lines" :key="n">{{ n }}</div></div>
        <textarea ref="area" v-model="text" spellcheck="false" wrap="off" @keydown="onKey" @scroll="syncScroll"></textarea>
      </div>
      <div class="buttons">
        <span class="muted small grow">{{ status || 'Ctrl+S saves. Scripts reload in Play when saved.' }}</span>
        <button @click="close">Close</button>
        <button class="primary" :disabled="!!error || loading" @click="save">Save</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.45);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 60;
}
.sheet {
  background: var(--panel, #222);
  border: 1px solid var(--border, #444);
  padding: 12px 14px;
  width: min(1000px, 94vw);
  height: min(720px, 90vh);
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.head,
.buttons {
  display: flex;
  align-items: center;
  gap: 8px;
}
h3 {
  margin: 0;
  font-family: Consolas, 'Cascadia Mono', monospace;
  font-size: 13px;
}
.dot {
  color: #f59e0b;
  margin-left: 6px;
}
.grow {
  flex: 1;
}
.small {
  font-size: 11px;
}
.muted {
  opacity: 0.6;
}
.error {
  color: #e66;
}
.code {
  flex: 1;
  min-height: 0;
  display: flex;
  border: 1px solid var(--border, #444);
  background: #15151c;
  font-family: Consolas, 'Cascadia Mono', monospace;
  font-size: 12.5px;
  line-height: 18px;
}
.gutter {
  overflow: hidden;
  padding: 6px 8px;
  text-align: right;
  color: #6b7280;
  user-select: none;
  min-width: 36px;
  border-right: 1px solid #2a2a3a;
}
textarea {
  flex: 1;
  resize: none;
  border: none;
  outline: none;
  background: transparent;
  color: #e5e7eb;
  padding: 6px 8px;
  font: inherit;
  line-height: inherit;
  tab-size: 4;
  white-space: pre;
}
</style>
