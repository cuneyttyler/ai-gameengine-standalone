<script setup lang="ts">
// One reflected field's editor, generated entirely from the registry's
// metadata (type, description, min/max, options) — the same for component
// fields and project settings. Emits `change` with the new value in its JSON
// form (enums by option name, entity references by id); the host validates
// it again before anything is written.
import { computed } from 'vue'

export type FieldType =
  | 'float'
  | 'int'
  | 'bool'
  | 'string'
  | 'enum'
  | 'entity'
  | 'texture'
  | 'script'
  | 'audio'

export interface FieldData {
  name: string
  type: FieldType
  description: string
  value: number | boolean | string | null
  options?: string[]
  min?: number
  max?: number
  overridden?: boolean
}

export interface Choices {
  entities: { id: number; label: string }[]
  textures: string[]
  audio: string[]
  scripts: string[]
}

const props = defineProps<{ field: FieldData; choices?: Choices; disabled?: boolean }>()
const emit = defineEmits<{ change: [value: unknown]; openScript: [path: string] }>()

// Bounded 0-1 values (tints, opacity, texture regions) also get a slider.
const isUnitRange = computed(
  () => props.field.type === 'float' && props.field.min === 0 && props.field.max === 1,
)

function fileName(path: string): string {
  return path.split('/').pop() ?? path
}

function emitNumber(event: Event): void {
  const value = Number((event.target as HTMLInputElement).value)
  if (!Number.isNaN(value)) {
    emit('change', value)
  }
}
</script>

<template>
  <span class="editor">
    <template v-if="field.type === 'float' || field.type === 'int'">
      <input
        v-if="isUnitRange"
        class="slider"
        type="range"
        min="0"
        max="1"
        step="0.01"
        :value="field.value as number"
        :disabled="disabled"
        @change="emitNumber"
      />
      <input
        class="number"
        :class="{ narrow: isUnitRange }"
        type="number"
        :step="field.type === 'int' ? 1 : isUnitRange ? 0.01 : 0.05"
        :min="field.min"
        :max="field.max"
        :value="field.value as number"
        :disabled="disabled"
        @change="emitNumber"
      />
    </template>

    <input
      v-else-if="field.type === 'bool'"
      type="checkbox"
      class="checkbox"
      :checked="field.value as boolean"
      :disabled="disabled"
      @change="emit('change', ($event.target as HTMLInputElement).checked)"
    />

    <input
      v-else-if="field.type === 'string'"
      type="text"
      spellcheck="false"
      :value="field.value as string"
      :disabled="disabled"
      @change="emit('change', ($event.target as HTMLInputElement).value)"
    />

    <select
      v-else-if="field.type === 'enum'"
      :value="field.value as string"
      :disabled="disabled"
      @change="emit('change', ($event.target as HTMLSelectElement).value)"
    >
      <option v-for="option in field.options" :key="option" :value="option">{{ option }}</option>
    </select>

    <select
      v-else-if="field.type === 'entity'"
      :value="field.value ?? ''"
      :disabled="disabled"
      @change="
        emit(
          'change',
          ($event.target as HTMLSelectElement).value === '' ? null : Number(($event.target as HTMLSelectElement).value),
        )
      "
    >
      <option value="">(none)</option>
      <option v-for="entity in choices?.entities ?? []" :key="entity.id" :value="entity.id">
        {{ entity.label }} #{{ entity.id }}
      </option>
    </select>

    <select
      v-else-if="field.type === 'texture' || field.type === 'audio'"
      :value="field.value as string"
      :title="field.value as string"
      :disabled="disabled"
      @change="emit('change', ($event.target as HTMLSelectElement).value)"
    >
      <option value="">{{ field.type === 'texture' ? '(built-in)' : '(none)' }}</option>
      <option
        v-for="path in (field.type === 'texture' ? choices?.textures : choices?.audio) ?? []"
        :key="path"
        :value="path"
      >
        {{ fileName(path) }}
      </option>
    </select>

    <span v-else-if="field.type === 'script'" class="script-field">
      <select
        :value="field.value as string"
        :title="field.value as string"
        :disabled="disabled"
        @change="emit('change', ($event.target as HTMLSelectElement).value)"
      >
        <option value="">(none)</option>
        <option v-for="script in choices?.scripts ?? []" :key="script" :value="script">
          {{ fileName(script) }}
        </option>
      </select>
      <button v-if="field.value" title="Open in your editor" @click="emit('openScript', field.value as string)">
        Open
      </button>
    </span>
  </span>
</template>

<style scoped>
.editor {
  display: flex;
  align-items: center;
  gap: 4px;
}
input,
select,
button {
  height: 22px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
}
.number,
input[type='text'] {
  width: 120px;
  padding: 0 4px;
}
.number.narrow {
  width: 52px;
}
.slider {
  width: 64px;
  height: 14px;
  accent-color: #3b82f6;
}
select {
  width: 128px;
  min-width: 0;
}
input:disabled,
select:disabled {
  opacity: 0.5;
}
.checkbox {
  width: 14px;
  height: 14px;
}
.script-field {
  display: flex;
  gap: 2px;
}
.script-field select {
  width: 84px;
}
.script-field button {
  padding: 0 6px;
  cursor: pointer;
}
</style>
