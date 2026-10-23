<script setup lang="ts">
// Conditions on a choice, a dialogue start or a quest branch (docs/systems/story.md):
// all must hold. Edits the JSON form in place: {"flag": n}, {"flag": n, "is": false},
// {"quest": id, "is": state, "stage": 20}, {"var": n, "op": ">=", "value": 3}, and the
// world's (15.2): {"item": id, "count": 2}, {"gold": n}, {"level": n},
// {"attribute": a, "atLeast": n}, {"time": {"from": h, "to": h}}, {"day": n},
// {"faction": f, "atLeast": n}.
export interface Condition {
  flag?: string
  quest?: string
  is?: boolean | string
  stage?: number
  var?: string
  op?: string
  value?: number | string
  item?: string
  count?: number
  gold?: number
  level?: number
  attribute?: string
  atLeast?: number
  time?: { from: number; to: number }
  day?: number
  faction?: string
}

const props = defineProps<{
  conditions: Condition[]
  quests: string[]
  stagesOf?: (quest: string) => number[]
}>()

type Kind = 'flag' | 'notFlag' | 'quest' | 'var' | 'item' | 'gold' | 'level' | 'attribute' | 'time' | 'day' | 'faction'

const kinds: Record<Kind, string> = {
  flag: 'flag is set',
  notFlag: "flag isn't set",
  quest: 'quest is',
  var: 'variable',
  item: 'player has item',
  gold: 'player has gold',
  level: 'player level',
  attribute: 'attribute',
  time: 'time of day',
  day: 'day',
  faction: 'reputation with',
}

function kindOf(c: Condition): Kind {
  if (c.flag !== undefined) return c.is === false ? 'notFlag' : 'flag'
  if (c.quest !== undefined) return 'quest'
  for (const k of ['var', 'item', 'gold', 'level', 'attribute', 'time', 'day', 'faction'] as const) {
    if (c[k] !== undefined) return k
  }
  return 'flag'
}

function setKind(c: Condition, kind: Kind): void {
  for (const key of Object.keys(c)) delete (c as Record<string, unknown>)[key]
  switch (kind) {
    case 'flag': c.flag = ''; break
    case 'notFlag': c.flag = ''; c.is = false; break
    case 'quest': c.quest = props.quests[0] ?? ''; c.is = 'active'; break
    case 'var': c.var = ''; c.op = '>='; c.value = 1; break
    case 'item': c.item = ''; c.count = 1; break
    case 'gold': c.gold = 10; break
    case 'level': c.level = 2; break
    case 'attribute': c.attribute = 'strength'; c.atLeast = 5; break
    case 'time': c.time = { from: 8, to: 20 }; break
    case 'day': c.day = 2; break
    case 'faction': c.faction = ''; c.atLeast = 10; break
  }
}

function setStage(c: Condition, value: string): void {
  if (value === '') delete c.stage
  else c.stage = Number(value)
}

// A variable's value: a number when it reads as one.
function setValue(c: Condition, text: string): void {
  c.value = text.trim() !== '' && !Number.isNaN(Number(text)) ? Number(text) : text
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
        <option v-for="(label, k) in kinds" :key="k" :value="k">{{ label }}</option>
      </select>
      <template v-if="kindOf(c) === 'flag' || kindOf(c) === 'notFlag'">
        <input v-model="c.flag" placeholder="flag name" spellcheck="false" />
      </template>
      <template v-else-if="kindOf(c) === 'quest'">
        <select v-model="c.quest">
          <option v-for="q in quests" :key="q" :value="q">{{ q }}</option>
        </select>
        <select v-model="c.is">
          <option value="inactive">not started</option>
          <option value="active">active</option>
          <option value="completed">completed</option>
          <option value="failed">failed</option>
        </select>
        <select
          v-if="c.is === 'active' && stagesOf"
          :value="c.stage ?? ''"
          title="At this stage"
          @change="setStage(c, ($event.target as HTMLSelectElement).value)"
        >
          <option value="">any stage</option>
          <option v-for="s in stagesOf(c.quest ?? '')" :key="s" :value="s">at stage {{ s }}</option>
        </select>
      </template>
      <template v-else-if="kindOf(c) === 'var'">
        <input v-model="c.var" placeholder="name" spellcheck="false" class="short" />
        <select v-model="c.op">
          <option v-for="op in ['==', '!=', '<', '<=', '>', '>=']" :key="op" :value="op">{{ op }}</option>
        </select>
        <input :value="c.value" class="short" @change="setValue(c, ($event.target as HTMLInputElement).value)" />
      </template>
      <template v-else-if="kindOf(c) === 'item'">
        <input v-model="c.item" placeholder="item id" spellcheck="false" class="short" />
        <span class="word">×</span>
        <input v-model.number="c.count" type="number" min="1" class="num" />
      </template>
      <template v-else-if="kindOf(c) === 'gold'">
        <span class="word">≥</span><input v-model.number="c.gold" type="number" min="0" class="num" />
      </template>
      <template v-else-if="kindOf(c) === 'level'">
        <span class="word">≥</span><input v-model.number="c.level" type="number" min="1" class="num" />
      </template>
      <template v-else-if="kindOf(c) === 'attribute'">
        <input v-model="c.attribute" placeholder="attribute" spellcheck="false" class="short" />
        <span class="word">≥</span><input v-model.number="c.atLeast" type="number" class="num" />
      </template>
      <template v-else-if="kindOf(c) === 'time' && c.time">
        <span class="word">from</span><input v-model.number="c.time.from" type="number" min="0" max="24" class="num" />
        <span class="word">to</span><input v-model.number="c.time.to" type="number" min="0" max="24" class="num" />
      </template>
      <template v-else-if="kindOf(c) === 'day'">
        <span class="word">≥</span><input v-model.number="c.day" type="number" min="1" class="num" />
      </template>
      <template v-else-if="kindOf(c) === 'faction'">
        <input v-model="c.faction" placeholder="faction" spellcheck="false" class="short" />
        <span class="word">≥</span><input v-model.number="c.atLeast" type="number" class="num" />
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
  width: 56px;
}
</style>
