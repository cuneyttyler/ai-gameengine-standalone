<script setup lang="ts">
// World ▸ Props & objects (the description) and Build ▸ Props & objects (each made: its Reaction, its states, its users):
// every thing in the places someone uses — what it's for, how it looks and changes, who uses it (the player, the
// villagers), casual or for a quest, when and why, and the engine's Reaction that makes each use so.
import { computed, ref } from 'vue'

interface Reaction {
  trigger?: string
  action?: string
  range?: number
  prompt?: string
  requires?: string
  takes?: string
  gives?: string
  accepts?: string
  requiresFlag?: string
  userClip?: string
  clip?: string
  effect?: string
  userEffect?: string
  seconds?: number
  becomes?: string
  once?: boolean
  cooldown?: number
  if?: unknown[]
  do?: unknown[]
  emits?: string
  setFlag?: string
  text?: string
}
interface Use {
  by?: string
  who?: string[]
  kind?: string
  quest?: { quest?: string; stage?: number }
  when?: string
  why?: string
  reaction?: Reaction
}
interface Anim {
  id?: string
  who?: string
  action?: string
  loop?: boolean
}
interface AnimMade {
  status?: string
  clip?: string
  error?: string
}
interface Thing {
  id?: string
  name?: string
  place?: string
  where?: string
  purpose?: string
  look?: string
  states?: { id?: string; look?: string }[]
  smart?: { type?: string; affordances?: string[] }
  uses?: Use[]
  animations?: Anim[]
}
interface Made {
  status?: string
  entity?: string
  scene?: string
  reactions?: number
  notes?: string[]
  error?: string
  animations?: Record<string, AnimMade>
}
const props = defineProps<{
  objects?: { objects?: Thing[] } | null
  built?: { objects?: Record<string, Made> } | null
  mode: 'design' | 'build'
}>()
const emit = defineEmits<{ (e: 'redesign'): void; (e: 'build', id: string): void }>()

const things = computed(() => (props.objects?.objects ?? []).filter((t): t is Thing => !!t && !!t.id))
const open = ref<Record<string, boolean>>({})
const madeOf = (id?: string) => (id ? props.built?.objects?.[id] : undefined)
const builtCount = computed(() => things.value.filter((t) => madeOf(t.id)?.status === 'built').length)
function short(id?: string): string {
  return (id ?? '').replace(/^(character|location)\//, '')
}
function reactionText(r?: Reaction): string {
  if (!r) return ''
  const parts: string[] = [`${r.trigger ?? 'input'}${r.action && r.trigger === 'input' ? ' ' + r.action : ''}`]
  if (r.prompt) parts.push(`“${r.prompt}”`)
  if (r.requires) parts.push(`needs ${r.requires}`)
  if (r.accepts) parts.push(`in hand ${r.accepts}`)
  if (r.takes) parts.push(`takes ${r.takes}`)
  if (r.gives) parts.push(`gives ${r.gives}`)
  if (r.userClip) parts.push(`user plays ${r.userClip}`)
  if (r.clip) parts.push(`plays ${r.clip}`)
  if (r.seconds) parts.push(`${r.seconds} s`)
  if (r.becomes) parts.push(`becomes ${r.becomes}`)
  if (r.once) parts.push('once')
  if (r.cooldown) parts.push(`every ${r.cooldown} s`)
  if (r.if?.length) parts.push(`if ${JSON.stringify(r.if)}`)
  if (r.do?.length) parts.push(`do ${JSON.stringify(r.do)}`)
  if (r.setFlag) parts.push(`sets ${r.setFlag}`)
  if (r.emits) parts.push(`emits ${r.emits}`)
  return parts.join(' · ')
}
</script>

<template>
  <div class="objects">
    <div class="bar">
      <span class="muted">{{ things.length ? (mode === 'build' ? `${builtCount}/${things.length} made` : `${things.length} props & objects`) : 'Not described yet.' }}</span>
      <span class="spacer" />
      <button @click="emit('redesign')">{{ things.length ? 'Describe them again' : 'Describe the props & objects' }}</button>
      <button v-if="mode === 'build'" class="primary" :disabled="!things.length" title="Each one's Reaction, states and users made in its scene"
              @click="emit('build', 'all')">Build all</button>
    </div>
    <div v-for="t in things" :key="t.id" class="thing">
      <div class="head" @click="open = { ...open, [t.id!]: !open[t.id!] }">
        <span class="caret">{{ open[t.id!] ? '▾' : '▸' }}</span>
        <b>{{ t.name || t.id }}</b> <span class="dim small">{{ short(t.place) }}{{ t.where ? ' · ' + t.where : '' }}</span>
        <span class="tag" v-for="k in [...new Set((t.uses ?? []).map((u) => u.kind))]" :key="k">{{ k }}</span>
        <span v-if="t.smart?.type" class="tag" title="Characters use it in their day">{{ t.smart.type }}</span>
        <span class="spacer" />
        <template v-if="mode === 'build'">
          <span class="status" :class="madeOf(t.id)?.status ?? 'todo'" :title="madeOf(t.id)?.error ?? ''">{{ madeOf(t.id)?.status ?? 'to make' }}</span>
          <button class="mini" @click.stop="emit('build', t.id!)">{{ madeOf(t.id)?.status === 'built' ? 'Make again' : 'Make' }}</button>
        </template>
      </div>
      <div v-if="open[t.id!]" class="body">
        <p><b>Purpose</b> {{ t.purpose }}</p>
        <p v-if="t.look"><b>Look</b> {{ t.look }}</p>
        <p v-if="t.states?.length"><b>States</b> <span v-for="s in t.states" :key="s.id" :title="s.look">{{ s.id }} </span></p>
        <p v-if="t.smart?.type"><b>In their day</b> a {{ t.smart.type }}: {{ (t.smart.affordances ?? []).join(', ') }}</p>
        <table v-if="t.uses?.length" class="grid">
          <thead><tr><th>Who</th><th>Kind</th><th>When</th><th>Why</th><th>Reaction</th></tr></thead>
          <tbody>
            <tr v-for="(u, i) in t.uses" :key="i">
              <td>{{ u.by === 'player' ? 'the player' : (u.who ?? []).map(short).join(', ') || u.by }}</td>
              <td>{{ u.kind }}<span v-if="u.quest?.quest" class="dim"> {{ u.quest.quest }} · {{ u.quest.stage }}</span></td>
              <td>{{ u.when }}</td>
              <td>{{ u.why }}</td>
              <td class="small mono">{{ reactionText(u.reaction) }}</td>
            </tr>
          </tbody>
        </table>
        <div v-if="t.animations?.length" class="anims">
          <b>Animations</b>
          <div v-for="a in t.animations" :key="a.id + (a.who ?? '')" class="small">
            <span class="mono">{{ a.id }}</span> — {{ a.who === 'object' ? 'the thing itself' : short(a.who) }}: {{ a.action }}{{ a.loop ? ' (loops)' : '' }}
            <span v-if="mode === 'build' && madeOf(t.id)?.animations?.[a.id!]" class="status" :class="madeOf(t.id)?.animations?.[a.id!]?.status"
                  :title="madeOf(t.id)?.animations?.[a.id!]?.error ?? ''">
              {{ madeOf(t.id)?.animations?.[a.id!]?.status }}{{ madeOf(t.id)?.animations?.[a.id!]?.clip && madeOf(t.id)?.animations?.[a.id!]?.clip !== a.id ? ' as ' + madeOf(t.id)?.animations?.[a.id!]?.clip : '' }}</span>
          </div>
        </div>
        <template v-if="mode === 'build' && madeOf(t.id)">
          <p class="small"><b>Made</b> {{ madeOf(t.id)?.entity }} in {{ madeOf(t.id)?.scene }} · {{ madeOf(t.id)?.reactions ?? 0 }} Reaction(s)</p>
          <ul v-if="madeOf(t.id)?.notes?.length" class="small dim"><li v-for="(n, i) in madeOf(t.id)?.notes" :key="i">{{ n }}</li></ul>
          <p v-if="madeOf(t.id)?.error" class="small err">{{ madeOf(t.id)?.error }}</p>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.objects { display: flex; flex-direction: column; gap: 6px; }
.bar { display: flex; align-items: center; gap: 8px; }
.spacer { flex: 1; }
.thing { border: 1px solid #2a2a3a; }
.head { display: flex; align-items: center; gap: 8px; padding: 6px 8px; cursor: pointer; user-select: none; }
.head:hover { background: #1e1e2e; }
.caret { width: 12px; color: #8a8aa0; }
.body { padding: 4px 12px 10px; }
.body p { margin: 4px 0; }
.tag { font-size: 0.8em; border: 1px solid #3a3a50; padding: 0 5px; color: #a6adc8; }
.status { font-size: 0.8em; border: 1px solid #45475a; padding: 0 5px; }
.status.built { color: #a6e3a1; border-color: #a6e3a1; }
.status.failed { color: #f38ba8; border-color: #f38ba8; }
.grid { width: 100%; border-collapse: collapse; margin: 6px 0; }
.grid th, .grid td { text-align: left; padding: 3px 6px; border-top: 1px solid #2a2a3a; vertical-align: top; font-size: 0.9em; }
.mono { font-family: monospace; }
.dim, .muted { color: #8a8aa0; }
.small { font-size: 0.85em; }
.err { color: #f38ba8; }
.anims { margin: 6px 0; }
</style>
