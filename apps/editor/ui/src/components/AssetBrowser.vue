<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue'
import { editorState } from '../editorState'
import { onHostMessage, sendToHost } from '../ipc'

interface Asset {
  path: string
  name: string
  type: 'texture' | 'tileset' | 'audio'
  error: string
  version: number
  width?: number
  height?: number
  tileWidth?: number
  tileHeight?: number
  columns?: number
  rows?: number
  duration?: number
}

// The host maps the open project folder to this virtual host
// (apps/editor/main.cpp, kProjectHostName).
const PROJECT_HOST = 'https://aige-project.local/'

const assets = ref<Asset[]>([])
const filter = ref('')
const selectedPath = ref<string | null>(null)
const selectedTile = ref(0)
const paintSolid = ref(false)

// Editable copies of the selected asset's import settings.
const settingsType = ref<'texture' | 'tileset'>('texture')
const settingsTileWidth = ref(64)
const settingsTileHeight = ref(32)

const visible = computed(() => {
  const query = filter.value.trim().toLowerCase()
  return query ? assets.value.filter((a) => a.path.toLowerCase().includes(query)) : assets.value
})
const selected = computed(() => assets.value.find((a) => a.path === selectedPath.value) ?? null)

function urlOf(asset: Asset): string {
  return `${PROJECT_HOST}${asset.path.split('/').map(encodeURIComponent).join('/')}?v=${asset.version}`
}

onHostMessage((message) => {
  if (message.type === 'assets:list') {
    assets.value = message.assets as Asset[]
    if (selectedPath.value && !assets.value.some((a) => a.path === selectedPath.value)) {
      selectedPath.value = null
    }
  } else if (message.type === 'assets:select') {
    selectedPath.value = message.path as string
  }
})

watch(selected, (asset) => {
  if (!asset || asset.type === 'audio') {
    return
  }
  settingsType.value = asset.type
  settingsTileWidth.value = asset.tileWidth ?? 64
  settingsTileHeight.value = asset.tileHeight ?? 32
  if (selectedTile.value >= (asset.columns ?? 1) * (asset.rows ?? 1)) {
    selectedTile.value = 0
  }
})

const settingsChanged = computed(() => {
  const asset = selected.value
  if (!asset || asset.type === 'audio') {
    return false
  }
  return (
    settingsType.value !== asset.type ||
    (settingsType.value === 'tileset' &&
      (settingsTileWidth.value !== asset.tileWidth || settingsTileHeight.value !== asset.tileHeight))
  )
})

function applySettings(): void {
  if (!selected.value) {
    return
  }
  sendToHost({
    type: 'asset:settings',
    path: selected.value.path,
    assetType: settingsType.value,
    tileWidth: settingsTileWidth.value,
    tileHeight: settingsTileHeight.value,
  })
}

// Tile picker: the tileset image scaled to fit, with one button per tile.
const PICKER_MAX_WIDTH = 240
const pickerScale = computed(() => {
  const asset = selected.value
  if (!asset?.width) {
    return 1
  }
  return Math.min(4, PICKER_MAX_WIDTH / asset.width)
})
const tiles = computed(() => {
  const asset = selected.value
  if (!asset || asset.type !== 'tileset' || !asset.columns || !asset.rows) {
    return []
  }
  const scale = pickerScale.value
  const result = []
  for (let i = 0; i < asset.columns * asset.rows; ++i) {
    const col = i % asset.columns
    const row = Math.floor(i / asset.columns)
    result.push({
      index: i,
      style: {
        left: `${col * asset.tileWidth! * scale}px`,
        top: `${row * asset.tileHeight! * scale}px`,
        width: `${asset.tileWidth! * scale}px`,
        height: `${asset.tileHeight! * scale}px`,
      },
    })
  }
  return result
})

function placeAsProp(): void {
  sendToHost({ type: 'tool:set', tool: 'place:sprite', asset: selectedPath.value, tile: selectedTile.value })
}

function paintTiles(): void {
  sendToHost({
    type: 'tool:set',
    tool: 'tile:asset',
    asset: selectedPath.value,
    tile: selectedTile.value,
    solid: paintSolid.value,
  })
}

// Keep an active brush in sync when the tile or solidity changes.
watch([selectedTile, paintSolid], () => {
  const brushActive =
    (editorState.tool === 'tile:asset' || editorState.tool === 'place:sprite') &&
    editorState.brush.asset === selectedPath.value
  if (brushActive) {
    sendToHost({
      type: 'tool:set',
      tool: editorState.tool,
      asset: selectedPath.value,
      tile: selectedTile.value,
      solid: paintSolid.value,
    })
  }
})

function applyToSelected(): void {
  sendToHost({ type: 'asset:apply', path: selectedPath.value, tile: selectedTile.value })
}

function preview(): void {
  sendToHost({ type: 'asset:preview', path: selectedPath.value })
}

function formatDuration(seconds?: number): string {
  return seconds === undefined ? '' : `${seconds.toFixed(2)} s`
}

onMounted(() => {
  sendToHost({ type: 'assets:request' })
})
</script>

<template>
  <div class="asset-browser">
    <div class="header">
      <h2>Assets</h2>
      <input v-model="filter" class="filter" type="search" placeholder="Filter" spellcheck="false" />
      <button title="Copy images or audio into the project" @click="sendToHost({ type: 'asset:import' })">
        Import…
      </button>
      <button title="Rescan the assets folder" @click="sendToHost({ type: 'assets:refresh' })">Refresh</button>
    </div>

    <div class="body">
      <div class="grid">
        <p v-if="assets.length === 0" class="placeholder">
          No assets yet. Import PNG/JPG images or WAV/MP3/FLAC audio.
        </p>
        <button
          v-for="asset in visible"
          :key="asset.path"
          class="card"
          :class="{ selected: asset.path === selectedPath, broken: !!asset.error }"
          :title="asset.error || asset.path"
          @click="selectedPath = asset.path"
        >
          <span class="thumb">
            <img v-if="asset.type !== 'audio' && !asset.error" :src="urlOf(asset)" alt="" draggable="false" />
            <span v-else class="glyph">{{ asset.type === 'audio' ? '♪' : '!' }}</span>
          </span>
          <span class="name">{{ asset.name }}</span>
          <span class="badge">{{ asset.type }}</span>
        </button>
      </div>

      <aside v-if="selected" class="details">
        <div class="title" :title="selected.path">{{ selected.path }}</div>
        <p v-if="selected.error" class="error">{{ selected.error }}</p>

        <template v-else-if="selected.type === 'audio'">
          <div class="row"><span>Duration</span><span>{{ formatDuration(selected.duration) }}</span></div>
          <div class="actions">
            <button @click="preview">▶ Preview</button>
          </div>
        </template>

        <template v-else>
          <div class="row"><span>Size</span><span>{{ selected.width }} × {{ selected.height }}</span></div>
          <div class="row">
            <span>Import as</span>
            <select v-model="settingsType">
              <option value="texture">Texture</option>
              <option value="tileset">Tileset</option>
            </select>
          </div>
          <div v-if="settingsType === 'tileset'" class="row">
            <span>Tile size</span>
            <span class="pair">
              <input v-model.number="settingsTileWidth" type="number" min="1" />
              ×
              <input v-model.number="settingsTileHeight" type="number" min="1" />
            </span>
          </div>
          <div v-if="settingsChanged" class="actions">
            <button class="primary" @click="applySettings">Apply import settings</button>
          </div>

          <div
            v-if="selected.type === 'tileset' && !settingsChanged"
            class="tile-picker"
            :style="{
              width: `${selected.width! * pickerScale}px`,
              height: `${selected.height! * pickerScale}px`,
            }"
          >
            <img :src="urlOf(selected)" alt="" draggable="false" />
            <button
              v-for="tile in tiles"
              :key="tile.index"
              class="tile"
              :class="{ active: tile.index === selectedTile }"
              :style="tile.style"
              :title="`Tile ${tile.index}`"
              @click="selectedTile = tile.index"
            ></button>
          </div>

          <div v-if="!settingsChanged" class="actions">
            <button title="Click cells in the viewport to paint ground tiles" @click="paintTiles">Paint tiles</button>
            <label class="check"><input v-model="paintSolid" type="checkbox" /> Solid</label>
            <button title="Click a cell in the viewport to place this as a standing object" @click="placeAsProp">
              Place as prop
            </button>
            <button title="Use this on the selected entity's sprite" @click="applyToSelected">Apply to selected</button>
          </div>
        </template>
      </aside>
    </div>
  </div>
</template>

<style scoped>
.asset-browser {
  display: flex;
  flex-direction: column;
  height: 100%;
  background: #181825;
  font-size: 0.85em;
}
.header {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 32px;
  padding: 0 8px;
  border-bottom: 1px solid #333;
  flex-shrink: 0;
}
h2 {
  font-size: 0.9em;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  opacity: 0.7;
  margin: 0 8px 0 0;
}
.filter {
  width: 160px;
}
button,
input,
select {
  height: 24px;
  padding: 0 8px;
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
}
button {
  cursor: pointer;
  white-space: nowrap;
}
button:hover {
  background: #262637;
}
button.primary {
  border-color: #3b82f6;
}
.body {
  flex: 1;
  display: flex;
  min-height: 0;
}
.grid {
  flex: 1;
  display: flex;
  flex-wrap: wrap;
  align-content: flex-start;
  gap: 6px;
  padding: 8px;
  overflow: auto;
}
.placeholder {
  opacity: 0.5;
  margin: 4px;
}
.card {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  width: 84px;
  height: auto;
  padding: 6px 4px 4px;
}
.card.selected {
  border-color: #3b82f6;
  background: #313148;
}
.card.broken {
  border-color: #ef4444;
}
.thumb {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 56px;
  height: 56px;
  background: repeating-conic-gradient(#222 0 25%, #2a2a2a 0 50%) 0 0 / 12px 12px;
}
.thumb img {
  max-width: 100%;
  max-height: 100%;
  image-rendering: pixelated;
}
.glyph {
  font-size: 22px;
  opacity: 0.7;
}
.name {
  width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 0.85em;
}
.badge {
  font-size: 0.7em;
  opacity: 0.5;
  text-transform: uppercase;
}
.details {
  width: 280px;
  flex-shrink: 0;
  border-left: 1px solid #333;
  padding: 8px;
  overflow: auto;
}
.title {
  font-weight: 600;
  margin-bottom: 8px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.error {
  color: #ef4444;
}
.row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
  margin-bottom: 6px;
}
.row > span:first-child {
  opacity: 0.7;
}
.pair input {
  width: 56px;
}
.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  margin: 8px 0;
}
.check {
  display: flex;
  align-items: center;
  gap: 4px;
}
.check input {
  height: auto;
}
.tile-picker {
  position: relative;
  margin: 8px 0;
  background: repeating-conic-gradient(#222 0 25%, #2a2a2a 0 50%) 0 0 / 12px 12px;
}
.tile-picker img {
  width: 100%;
  height: 100%;
  image-rendering: pixelated;
  display: block;
}
.tile {
  position: absolute;
  padding: 0;
  height: auto;
  border: 1px solid rgba(255, 255, 255, 0.12);
  background: transparent;
}
.tile:hover {
  background: rgba(59, 130, 246, 0.2);
}
.tile.active {
  border: 2px solid #3b82f6;
  background: rgba(59, 130, 246, 0.15);
}
</style>
