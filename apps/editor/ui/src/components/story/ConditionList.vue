<script setup lang="ts">
// Conditions on a choice or a dialogue start (docs/story.md): all must hold.
// Edits the JSON form in place: {"flag": n}, {"flag": n, "is": false},
// {"quest": id, "is": "inactive" | "active" | "completed"}.
export interface Condition {
  flag?: string
  quest?: string
  is?: boolean | string
}

const props = defineProps<{ conditions: Condition[]; quests: string[] }>()

type Kind = 'flag' | 'notFlag' | 'quest'

function kindOf(c: Condition): Kind {
  if (c.flag !== undefined) return c.is === false ? 'notFlag' : 'flag'
  return 'quest'
}

function setKind(c: Condition, kind: Kind): void {
  if (kind === 'quest') {
    delete c.flag
    c.quest = c.quest ?? props.quests[0] ?? ''
    c.is = 'active'
  } else {
    const name = c.flag ?? ''
    delete c.quest
    c.flag = name
    if (kind === 'notFlag') c.is = false
    else delete c.is
  }
}

function add(): void {
  props.conditions.push({ flag: '' })
}
</script>

<template>
  <div class="conditions">
    <div v-for="(c, i) in conditions" :key="i" class="row">
      <span class="word">if</span>
      <select :value="kindOf(c)" @change="setKind(c, ($event.target as HTMLSelectElement).value as Kind)">
        <option value="flag">flag is set</option>
        <option value="notFlag">flag isn't set</option>
        <option value="quest">quest is</option>
      </select>
      <input v-if="c.flag !== undefined" v-model="c.flag" placeholder="flag name" spellcheck="false" />
      <template v-else>
        <select v-model="c.quest">
          <option v-for="q in quests" :key="q" :value="q">{{ q }}</option>
        </select>
        <select v-model="c.is">
          <option value="inactive">not started</option>
          <option value="active">active</option>
          <option value="completed">completed</option>
        </select>
      </template>
      <button title="Remove this condition" @click="conditions.splice(i, 1)">×</button>
    </div>
    <button class="add" @click="add">+ Condition</button>
  </div>
</template>

<style scoped>
.conditions {
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.row {
  display: flex;
  gap: 4px;
  align-items: center;
}
.word {
  font-size: 0.8em;
  opacity: 0.6;
  width: 14px;
}
.add {
  align-self: flex-start;
  font-size: 0.85em;
}
input {
  width: 140px;
}
</style>
