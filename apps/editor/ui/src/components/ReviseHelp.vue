<script setup lang="ts">
// What a revision can do at this step (docs/places/revise-language.md): the revision language's
// instructions, from the build (data/places/revise-language.json), each with what it does and how
// it's said. An example clicked goes into the revision box.
import { ref } from 'vue'

export interface ReviseInstruction {
  do: string
  params?: Record<string, string>
  required?: string[]
  says?: string
  shown?: string
  examples?: string[]
}

defineProps<{ instructions: ReviseInstruction[] }>()
const emit = defineEmits<{ (e: 'use', text: string): void }>()
const open = ref(false)
</script>

<template>
  <div v-if="instructions.length" class="revise-help">
    <button class="small link" @click="open = !open">{{ open ? 'hide' : 'what can I ask?' }}</button>
    <dl v-if="open">
      <template v-for="i in instructions" :key="i.do">
        <dt>
          <code>{{ i.do }}</code>
          <span class="dim small">{{ Object.keys(i.params ?? {}).join(', ') }}</span>
        </dt>
        <dd>
          {{ i.says }}
          <span v-for="e in i.examples ?? []" :key="e" class="example" title="Put it in the box" @click="emit('use', e)">“{{ e }}”</span>
        </dd>
      </template>
    </dl>
  </div>
</template>

<style scoped>
.revise-help {
  margin: 2px 0 4px;
}
dl {
  margin: 4px 0;
  padding: 6px 8px;
  border: 1px solid #2a2a3a;
  background: #15151f;
  font-size: 0.9em;
}
dt {
  margin-top: 4px;
}
dt code {
  color: #93c5fd;
  margin-right: 6px;
}
dd {
  margin: 0 0 2px 12px;
  opacity: 0.85;
}
.example {
  margin-left: 6px;
  color: #a5b4fc;
  cursor: pointer;
}
.example:hover {
  text-decoration: underline;
}
.dim {
  opacity: 0.6;
}
.small {
  font-size: 0.85em;
}
</style>
