<script setup lang="ts">
// File ▸ Preferences (development_plan.md 4.2): the per-user choices, in one
// place (also reachable from the AI panel's ⚙).
import { nextTick, ref, watch } from 'vue'
import { editorState, setAiEnabled } from '../editorState'
import { sendToHost } from '../ipc'
import { trackOverlay, untrackOverlay } from '../overlay'
import { projectState } from '../projects'

const backdrop = ref<HTMLElement | null>(null)

watch(
  () => projectState.showPreferences,
  async (open) => {
    await nextTick()
    if (open && backdrop.value) {
      trackOverlay(backdrop.value)
      backdrop.value.focus()
    }
  },
)

function close(): void {
  if (backdrop.value) untrackOverlay(backdrop.value)
  projectState.showPreferences = false
}
</script>

<template>
  <div
    v-if="projectState.showPreferences"
    ref="backdrop"
    class="backdrop ed-form"
    tabindex="-1"
    @mousedown.self="close"
    @keydown.esc.stop="close"
  >
    <div class="sheet">
      <header>
        <h3>Preferences</h3>
        <button title="Close (Esc)" @click="close">×</button>
      </header>
      <p class="muted">These are yours, on this computer — not part of the project.</p>
      <section>
        <h4>AI</h4>
        <label class="row">
          <input
            type="checkbox"
            :checked="editorState.ai.enabled"
            @change="setAiEnabled(($event.target as HTMLInputElement).checked)"
          />
          Use AI (when off, the AI service isn't started and nothing is sent to a model)
        </label>
        <label class="row">
          <input
            type="radio"
            name="pref-review"
            :checked="editorState.ai.reviewPolicy === 'reviewAll'"
            :disabled="!editorState.ai.enabled"
            @change="sendToHost({ type: 'ai:setReviewPolicy', policy: 'reviewAll' })"
          />
          Review every AI change before it's applied
        </label>
        <label class="row">
          <input
            type="radio"
            name="pref-review"
            :checked="editorState.ai.reviewPolicy === 'autoApplyTweaks'"
            :disabled="!editorState.ai.enabled"
            @change="sendToHost({ type: 'ai:setReviewPolicy', policy: 'autoApplyTweaks' })"
          />
          Apply small tweaks directly (with Undo)
        </label>
      </section>
      <section>
        <h4>Project</h4>
        <p class="muted">{{ projectState.current }}</p>
        <label class="row">
          <input
            type="checkbox"
            :checked="projectState.openLast"
            @change="sendToHost({ type: 'prefs:openLast', on: ($event.target as HTMLInputElement).checked })"
          />
          Open the last project directly at launch (otherwise the welcome window; File ▸ Close Project shows it)
        </label>
        <p class="muted">Project-wide settings (camera, combat, time…) are in File ▸ Project Settings…</p>
      </section>
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
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
h3 {
  margin: 0;
}
h4 {
  margin: 12px 0 6px;
  font-size: 0.8em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.6;
}
.row {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 3px 0;
}
.muted {
  opacity: 0.6;
  font-size: 0.9em;
  margin: 4px 0;
  word-break: break-all;
}
</style>
