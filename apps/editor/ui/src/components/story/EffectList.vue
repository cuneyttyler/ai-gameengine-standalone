<script setup lang="ts">
// What a choice, node, quest stage or finished quest does (docs/story.md). Edits
// the JSON form in place: {"op": "set_flag", "flag": n}, {"op": "start_quest",
// "quest": id}, {"op": "set_stage", "quest": id, "stage": 20}, {"op": "set_var",
// "var": n, "value": v}, {"op": "give_item", "item": id, "count": 2},
// {"op": "give_gold", "amount": 10}, {"op": "play_cutscene", "cutscene": id}, ...
export interface Effect {
  op: string
  flag?: string
  quest?: string
  objective?: string
  stage?: number
  var?: string
  value?: number | string | boolean
  item?: string
  count?: number
  amount?: number
  faction?: string
  cutscene?: string
}

const props = defineProps<{
  effects: Effect[]
  quests: string[]
  objectivesOf: (quest: string) => string[]
  stagesOf?: (quest: string) => number[]
}>()

const ops: Record<string, string> = {
  set_flag: 'set flag',
  clear_flag: 'clear flag',
  start_quest: 'start quest',
  complete_quest: 'complete quest',
  fail_quest: 'fail quest',
  complete_objective: 'complete objective',
  set_stage: 'set quest stage',
  set_var: 'set variable',
  add_var: 'add to variable',
  give_item: 'give item',
  take_item: 'take item',
  give_gold: 'give gold',
  give_xp: 'give experience',
  reputation: 'change reputation',
  play_cutscene: 'play cutscene',
}

const questOps = ['start_quest', 'complete_quest', 'fail_quest', 'complete_objective', 'set_stage']

function setOp(e: Effect, op: string): void {
  const quest = e.quest
  for (const key of Object.keys(e)) delete (e as unknown as Record<string, unknown>)[key]
  e.op = op
  if (op === 'set_flag' || op === 'clear_flag') e.flag = ''
  else if (questOps.includes(op)) {
    e.quest = quest ?? props.quests[0] ?? ''
    if (op === 'complete_objective') e.objective = props.objectivesOf(e.quest)[0] ?? ''
    if (op === 'set_stage') e.stage = props.stagesOf?.(e.quest)[0] ?? 10
  } else if (op === 'set_var' || op === 'add_var') {
    e.var = ''
    e.value = 1
  } else if (op === 'give_item' || op === 'take_item') {
    e.item = ''
    e.count = 1
  } else if (op === 'give_gold' || op === 'give_xp') e.amount = 10
  else if (op === 'reputation') {
    e.faction = ''
    e.value = 10
  } else if (op === 'play_cutscene') e.cutscene = ''
}

function setValue(e: Effect, text: string): void {
  e.value = text.trim() !== '' && !Number.isNaN(Number(text)) ? Number(text) : text
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
      <input v-if="e.op === 'set_flag' || e.op === 'clear_flag'" v-model="e.flag" placeholder="flag name" spellcheck="false" />
      <template v-else-if="questOps.includes(e.op)">
        <select v-model="e.quest">
          <option v-for="q in quests" :key="q" :value="q">{{ q }}</option>
        </select>
        <select v-if="e.op === 'complete_objective'" v-model="e.objective">
          <option v-for="o in objectivesOf(e.quest ?? '')" :key="o" :value="o">{{ o }}</option>
        </select>
        <select v-if="e.op === 'set_stage' && stagesOf" v-model.number="e.stage">
          <option v-for="s in stagesOf(e.quest ?? '')" :key="s" :value="s">stage {{ s }}</option>
        </select>
        <input v-else-if="e.op === 'set_stage'" v-model.number="e.stage" type="number" class="num" />
      </template>
      <template v-else-if="e.op === 'set_var' || e.op === 'add_var'">
        <input v-model="e.var" placeholder="variable" spellcheck="false" class="short" />
        <span class="word">{{ e.op === 'set_var' ? 'to' : 'by' }}</span>
        <input :value="e.value" class="short" @change="setValue(e, ($event.target as HTMLInputElement).value)" />
      </template>
      <template v-else-if="e.op === 'give_item' || e.op === 'take_item'">
        <input v-model="e.item" placeholder="item id" spellcheck="false" class="short" />
        <span class="word">×</span><input v-model.number="e.count" type="number" min="1" class="num" />
      </template>
      <input v-else-if="e.op === 'give_gold' || e.op === 'give_xp'" v-model.number="e.amount" type="number" class="num" />
      <template v-else-if="e.op === 'reputation'">
        <input v-model="e.faction" placeholder="faction" spellcheck="false" class="short" />
        <span class="word">by</span><input v-model.number="e.value" type="number" class="num" />
      </template>
      <input v-else-if="e.op === 'play_cutscene'" v-model="e.cutscene" placeholder="cutscene id" spellcheck="false" />
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
  flex-wrap: wrap;
}
.word {
  font-size: 0.8em;
  opacity: 0.6;
  min-width: 14px;
}
.add {
  align-self: flex-start;
  font-size: 0.85em;
}
input {
  width: 140px;
}
input.short {
  width: 100px;
}
input.num {
  width: 64px;
}
</style>
