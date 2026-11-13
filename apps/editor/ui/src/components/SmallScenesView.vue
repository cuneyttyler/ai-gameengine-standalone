<script setup lang="ts">
// Life ▸ Small scenes (the description) and Build ▸ Small scenes (each made: an ambient cutscene the player keeps control
// through, and its trigger): the little scenes that play round the player as they come by — casual (the place's life) or
// about a quest active then — where, what starts them, how often, who's in them and what they do and say, beat by beat.
import { computed, ref } from 'vue'

interface Beat {
  who?: string
  do?: string
  line?: string
  to?: string
  object?: string
  clip?: string
  seconds?: number
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
interface SmallScene {
  id?: string
  name?: string
  kind?: string
  quests?: { quest?: string; stages?: number[] }[]
  place?: string
  area?: { anchor?: string; near?: string; radius?: number; room?: string }
  trigger?: string
  time?: string
  repeat?: string
  cooldown?: number
  cast?: string[]
  objects?: string[]
  beats?: Beat[]
  animations?: Anim[]
  after?: string
  why?: string
}
interface Made {
  status?: string
  cutscene?: string
  trigger?: string
  scene?: string
  notes?: string[]
  error?: string
  animations?: Record<string, AnimMade>
}
const props = defineProps<{
  scenes?: { scenes?: SmallScene[] } | null
  built?: { scenes?: Record<string, Made> } | null
  mode: 'design' | 'build'
}>()
const emit = defineEmits<{ (e: 'redesign'): void; (e: 'build', id: string): void }>()

const list = computed(() => (props.scenes?.scenes ?? []).filter((s): s is SmallScene => !!s && !!s.id))
// By place, in the order they first appear.
const byPlace = computed(() => {
  const m = new Map<string, SmallScene[]>()
  for (const s of list.value) {
    const k = s.place ?? ''
    if (!m.has(k)) m.set(k, [])
    m.get(k)!.push(s)
  }
  return [...m.entries()]
})
const open = ref<Record<string, boolean>>({})
const madeOf = (id?: string) => (id ? props.built?.scenes?.[id] : undefined)
const builtCount = computed(() => list.value.filter((s) => madeOf(s.id)?.status === 'built').length)
function short(id?: string): string {
  return (id ?? '').replace(/^(character|location)\//, '')
}
function whereText(s: SmallScene): string {
  const a = s.area ?? {}
  if (a.room) return `entering ${short(a.room)}`
  return `within ${a.radius ?? 6} of ${a.anchor || a.near || '?'}${s.time ? ' · ' + s.time : ''}`
}
function questsText(s: SmallScene): string {
  return (s.quests ?? []).map((q) => `${q.quest}${q.stages?.length ? ' ' + q.stages.join(', ') : ''}`).join('; ')
}
function beatText(b: Beat): string {
  const who = short(b.who)
  switch (b.do) {
    case 'say': return `${who}: “${b.line ?? ''}”`
    case 'walk': return `${who} walks to ${short(b.to)}`
    case 'face': return `${who} turns to ${short(b.to)}`
    case 'use': return `${who} uses ${b.object}${b.clip ? ' (' + b.clip + ')' : ''}`
    case 'animate': return `${who} ${b.clip ?? 'moves'}`
    case 'emote': return `${who} ${b.clip ?? b.line ?? 'reacts'}`
    default: return `${who} waits${b.seconds ? ' ' + b.seconds + ' s' : ''}`
  }
}
</script>

<template>
  <div class="small-scenes">
    <div class="bar">
      <span class="muted">{{ list.length ? (mode === 'build' ? `${builtCount}/${list.length} made` : `${list.length} small scenes`) : 'Not written yet.' }}</span>
      <span class="spacer" />
      <button @click="emit('redesign')">{{ list.length ? 'Write them again' : 'Write the small scenes' }}</button>
      <button v-if="mode === 'build'" class="primary" :disabled="!list.length" title="Each one's ambient cutscene and its trigger" @click="emit('build', 'all')">Build all</button>
    </div>
    <section v-for="[place, scenes] in byPlace" :key="place">
      <h4 class="place">{{ short(place) || 'Somewhere' }} <span class="dim small">{{ scenes.length }}</span></h4>
      <div v-for="s in scenes" :key="s.id" class="scene">
        <div class="head" @click="open = { ...open, [s.id!]: !open[s.id!] }">
          <span class="caret">{{ open[s.id!] ? '▾' : '▸' }}</span>
          <b>{{ s.name || s.id }}</b>
          <span class="tag" :class="s.kind">{{ s.kind }}</span>
          <span v-if="s.kind === 'quest'" class="dim small">{{ questsText(s) }}</span>
          <span class="dim small">{{ whereText(s) }} · {{ s.repeat }}{{ s.repeat === 'cooldown' ? ' ' + s.cooldown + ' s' : '' }}</span>
          <span class="spacer" />
          <template v-if="mode === 'build'">
            <span class="status" :class="madeOf(s.id)?.status ?? 'todo'" :title="madeOf(s.id)?.error ?? ''">{{ madeOf(s.id)?.status ?? 'to make' }}</span>
            <button class="mini" @click.stop="emit('build', s.id!)">{{ madeOf(s.id)?.status === 'built' ? 'Make again' : 'Make' }}</button>
          </template>
        </div>
        <div v-if="open[s.id!]" class="body">
          <p class="dim small">{{ (s.cast ?? []).map(short).join(', ') }}{{ s.objects?.length ? ' · uses ' + s.objects.join(', ') : '' }}</p>
          <ol class="beats"><li v-for="(b, i) in s.beats" :key="i">{{ beatText(b) }}</li></ol>
          <div v-if="s.animations?.length" class="small">
            <b>Animations</b>
            <div v-for="a in s.animations" :key="a.id + (a.who ?? '')">
              <span class="mono">{{ a.id }}</span> — {{ short(a.who) }}: {{ a.action }}
              <span v-if="mode === 'build' && madeOf(s.id)?.animations?.[a.id!]" class="status" :class="madeOf(s.id)?.animations?.[a.id!]?.status"
                    :title="madeOf(s.id)?.animations?.[a.id!]?.error ?? ''">{{ madeOf(s.id)?.animations?.[a.id!]?.status }}</span>
            </div>
          </div>
          <p v-if="s.after" class="small"><b>After</b> {{ s.after }}</p>
          <p v-if="s.why" class="small dim">{{ s.why }}</p>
          <template v-if="mode === 'build' && madeOf(s.id)">
            <p class="small"><b>Made</b> {{ madeOf(s.id)?.cutscene }} · trigger {{ madeOf(s.id)?.trigger }}</p>
            <ul v-if="madeOf(s.id)?.notes?.length" class="small dim"><li v-for="(n, i) in madeOf(s.id)?.notes" :key="i">{{ n }}</li></ul>
            <p v-if="madeOf(s.id)?.error" class="small err">{{ madeOf(s.id)?.error }}</p>
          </template>
        </div>
      </div>
    </section>
  </div>
</template>

<style scoped>
.small-scenes { display: flex; flex-direction: column; gap: 6px; }
.bar { display: flex; align-items: center; gap: 8px; }
.spacer { flex: 1; }
.place { margin: 10px 0 4px; border-bottom: 1px solid #2a2a3a; padding-bottom: 3px; }
.scene { border: 1px solid #2a2a3a; margin-bottom: 4px; }
.head { display: flex; align-items: center; gap: 8px; padding: 6px 8px; cursor: pointer; user-select: none; }
.head:hover { background: #1e1e2e; }
.caret { width: 12px; color: #8a8aa0; }
.body { padding: 4px 12px 10px; }
.body p { margin: 4px 0; }
.beats { margin: 4px 0 4px 18px; padding: 0; }
.tag { font-size: 0.8em; border: 1px solid #3a3a50; padding: 0 5px; color: #a6adc8; }
.tag.quest { color: #f9e2af; border-color: #f9e2af; }
.status { font-size: 0.8em; border: 1px solid #45475a; padding: 0 5px; }
.status.built { color: #a6e3a1; border-color: #a6e3a1; }
.status.failed { color: #f38ba8; border-color: #f38ba8; }
.dim, .muted { color: #8a8aa0; }
.small { font-size: 0.85em; }
.err { color: #f38ba8; }
.mono { font-family: monospace; }
</style>
