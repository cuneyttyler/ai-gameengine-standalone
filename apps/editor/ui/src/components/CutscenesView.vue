<script setup lang="ts">
// ⑤ Life ▸ Cutscenes: every moment the player watches rather than plays, as the cast timeline (⑥b) plans it — the story
// scene it is, the quest stage that starts it, where, its trigger, who is in it and what each does, its beats, what
// follows. How it starts is said in full (the one moment in the game that sets it off); on Build ▸ Scenes, what was made
// for it too (its tracks and that start), or why it couldn't be.
import { computed, ref } from 'vue'

interface Cutscene {
  id?: string
  name?: string
  scene?: string
  quest?: string
  stage?: number
  on?: string
  place?: string
  trigger?: { kind?: string; from?: string; spot?: string; with?: string; who?: string; thing?: string; name?: string; to?: string; event?: string }
  cast?: { who?: string; role?: string; does?: string }[]
  beats?: string[]
  after?: string
}
// built: by cutscene id, what Build ▸ Scenes made of it ({status, starts, error}).
const props = defineProps<{ cutscenes?: Cutscene[] | null; watched?: string[]; built?: Record<string, { status?: string; starts?: string; error?: string }> }>()
const emit = defineEmits<{ (e: 'redesign'): void }>()

const list = computed(() => (props.cutscenes ?? []).filter((c): c is Cutscene => !!c))
const missing = computed(() => (props.watched ?? []).filter((s) => !list.value.some((c) => c.scene === s)))
const open = ref<Record<string, boolean>>({})
function short(id?: string): string {
  return (id ?? '').replace(/^(character|location)\//, '')
}
// How it starts, in words (as the build says it, GameBuild::CutsceneStartText).
function startText(c: Cutscene): string {
  const t = c.trigger ?? {}
  const stage = c.quest ? `${c.quest} stage ${c.stage}` : ''
  const during = !stage ? '' : c.on === 'complete' ? `, once ${stage} is done` : `, while ${stage} is on`
  const place = short(c.place)
  switch (t.kind) {
    case 'reach': return `When the player reaches ${t.spot ?? t.to ?? '?'} in ${place}${during}`
    case 'leave': return `When the player comes out of ${short(t.from)} into ${place}${during}`
    case 'talk-end': return `When the player's conversation with ${short(t.with)} ends${during}`
    case 'defeat': return `When ${short(t.who)} is defeated${during}`
    case 'use': return `When the player uses the ${t.thing ?? '?'} in ${place}${during}`
    case 'event': return `On the event ${t.name ?? t.event ?? '?'}${during}`
    default: return !stage ? 'As the game starts' : c.on === 'complete' ? `As ${stage} is done` : `As ${stage} begins`
  }
}
const builtOf = (c: Cutscene) => props.built?.[c.id ?? ''] ?? null
</script>

<template>
  <div class="cutscenes">
    <div class="bar">
      <span class="muted">{{ list.length ? `${list.length} cutscene(s), planned with the cast timeline` : 'No cutscenes planned.' }}</span>
      <span class="spacer" />
      <button title="The cutscenes are planned with the cast timeline: it's made again" @click="emit('redesign')">Regenerate the cast timeline</button>
    </div>
    <p v-if="missing.length" class="err small">
      Watched scenes without a cutscene: {{ missing.join(', ') }}. Regenerate the cast timeline to plan them.
    </p>
    <div v-for="c in list" :key="c.id ?? c.scene" class="cut">
      <div class="head" @click="open = { ...open, [c.id ?? c.scene ?? '']: !open[c.id ?? c.scene ?? ''] }">
        <span class="caret">{{ open[c.id ?? c.scene ?? ''] ? '▾' : '▸' }}</span>
        <b>{{ c.name || c.id }}</b>
        <span class="tag">{{ c.scene }}</span>
        <span class="dim small">{{ short(c.place) }}</span>
        <span class="spacer" />
        <span v-if="builtOf(c)" class="small" :class="{ err: builtOf(c)?.status === 'failed', ok: builtOf(c)?.status === 'built' }">{{ builtOf(c)?.status }}</span>
      </div>
      <p class="starts"><b>Starts</b> {{ startText(c) }}.</p>
      <p v-if="builtOf(c)?.status === 'built' && builtOf(c)?.starts" class="starts dim small">Built: it starts {{ builtOf(c)?.starts }}.</p>
      <p v-if="builtOf(c)?.status === 'failed'" class="starts err small">{{ builtOf(c)?.error }}</p>
      <div v-if="open[c.id ?? c.scene ?? '']" class="body">
        <table v-if="c.cast?.length" class="grid">
          <thead><tr><th>Who</th><th>Role</th><th>Does</th></tr></thead>
          <tbody><tr v-for="(m, i) in c.cast" :key="i"><td>{{ m.who === 'player' ? 'the player' : short(m.who) }}</td><td>{{ m.role }}</td><td>{{ m.does }}</td></tr></tbody>
        </table>
        <ol v-if="c.beats?.length" class="beats"><li v-for="(b, i) in c.beats" :key="i">{{ b }}</li></ol>
        <p v-if="c.after" class="small"><b>After</b> {{ c.after }}</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.cutscenes { display: flex; flex-direction: column; gap: 6px; }
.bar { display: flex; align-items: center; gap: 8px; }
.spacer { flex: 1; }
.cut { border: 1px solid #2a2a3a; }
.head { display: flex; align-items: center; gap: 8px; padding: 6px 8px; cursor: pointer; user-select: none; }
.head:hover { background: #1e1e2e; }
.caret { width: 12px; color: #8a8aa0; }
.body { padding: 4px 12px 10px; }
.beats { margin: 4px 0 4px 18px; padding: 0; }
.tag { font-size: 0.8em; border: 1px solid #3a3a50; padding: 0 5px; color: #a6adc8; }
.grid { width: 100%; border-collapse: collapse; margin: 6px 0; }
.grid th, .grid td { text-align: left; padding: 3px 6px; border-top: 1px solid #2a2a3a; vertical-align: top; font-size: 0.9em; }
.dim, .muted { color: #8a8aa0; }
.small { font-size: 0.85em; }
.err { color: #f38ba8; }
.ok { color: #a6e3a1; }
.starts { margin: 0; padding: 0 8px 6px 28px; }
</style>
