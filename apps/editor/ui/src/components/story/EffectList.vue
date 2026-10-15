<script setup lang="ts">
// What a choice, node or completed quest does (docs/story.md). Edits the
// JSON form in place: {"op": "set_flag", "flag": n}, {"op": "start_quest",
// "quest": id}, {"op": "complete_objective", "quest": id, "objective": o}, ...
export interface Effect {
  op: string
  flag?: string
  quest?: string
  objective?: string
}

const props = defineProps<{
  effects: Effect[]
  quests: string[]
  objectivesOf: (quest: string) => string[]
}>()

const ops: Record<string, string> = {
  set_flag: 'set flag',
  clear_flag: 'clear flag',
  start_quest: 'start quest',
  complete_quest: 'complete quest',
  complete_objective: 'complete objective',
}

function isFlag(op: string): boolean {
  return op === 'set_flag' || op === 'clear_flag'
}

function setOp(e: Effect, op: string): void {
  const flagNow = isFlag(op)
  if (flagNow && !isFlag(e.op)) {
    delete e.quest
    delete e.objective
    e.flag = ''
  } else if (!flagNow) {
    delete e.flag
    e.quest = e.quest ?? props.quests[0] ?? ''
    if (op === 'complete_objective') e.objective = props.objectivesOf(e.quest)[0] ?? ''
    else delete e.objective
  }
  e.op = op
}

function add(): void {
  props.effects.push({ op: 'set_flag', flag: '' })
}
</script>

<template>
  <div class="effects">
    <div v-for="(e, i) in effects" :key="i" class="row">
      <span class="word">do</span>
      <select :value="e.op" @change="setOp(e, ($event.target as HTMLSelectElement).value)">
        <option v-for="(label, op) in ops" :key="op" :value="op">{{ label }}</option>
      </select>
      <input v-if="isFlag(e.op)" v-model="e.flag" placeholder="flag name" spellcheck="false" />
      <template v-else>
        <select v-model="e.quest">
          <option v-for="q in quests" :key="q" :value="q">{{ q }}</option>
        </select>
        <select v-if="e.op === 'complete_objective'" v-model="e.objective">
          <option v-for="o in objectivesOf(e.quest ?? '')" :key="o" :value="o">{{ o }}</option>
        </select>
      </template>
      <button title="Remove" @click="effects.splice(i, 1)">×</button>
    </div>
    <button class="add" @click="add">+ Effect</button>
  </div>
</template>

<style scoped>
.effects {
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
