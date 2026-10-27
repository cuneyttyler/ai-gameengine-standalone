<script setup lang="ts">
import { computed, nextTick, onMounted, ref, watch } from 'vue'
import { armPrefab, editorState } from '../editorState'
import PrefabThumb from './PrefabThumb.vue'
import CatalogReview from './CatalogReview.vue'
import LibraryTab from './LibraryTab.vue'
import WebSearchTab from './WebSearchTab.vue'
import { openContextMenu, separator, type MenuItem } from '../contextMenu'
import { prefabMenuExtensions } from '../assetMenus'
import { panelTab } from '../paneltabs'
import { onHostMessage, sendToHost } from '../ipc'
import { askMoveManyToFolder, askMoveToFolder, foldersOf, inFolder, moveItem, renameItem, type Root } from '../organize'
import { clickItem, emptySelection, keepShown, renamed, selectAll, type Selection } from '../selection'
import { confirmDialog, confirmWithOption, promptDialog } from '../dialogs'
import { openPackImport } from '../packImport'

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
  // Made in the Art tab (3.6): from its .gen.json.
  generation?: { description: string; variant: string | null; model: string; created: string }
  // What the art is (6.2): description, kind, tags, direction, cells, blocks,
  // name (suggested), edges (border terrains of a ground tile).
  analysis?: Record<string, string>
}

// The host maps the open project folder to this virtual host
// (apps/editor/main.cpp, kProjectHostName).
const PROJECT_HOST = 'https://aige-project.local/'

const assets = ref<Asset[]>([])
const filter = ref('')
const selectedPath = ref<string | null>(null)
const selectedTile = ref(0)
const paintSolid = ref(false)
// How the art is drawn (5.2): top-down art lies flat on the grid's diamonds;
// isometric art (a diamond, or a block standing on one) stands as it is.
const artFlat = ref(true)

// Editable copies of the selected asset's import settings.
const settingsType = ref<'texture' | 'tileset'>('texture')
const settingsTileWidth = ref(64)
const settingsTileHeight = ref(32)

// Organizing (development_plan.md 4.4): a folder, a kind and a search, per tab.
const prefabFolder = ref('')
const assetFolder = ref('')
const prefabKind = ref<'all' | 'tile' | 'character' | 'item' | 'other'>('all')
const assetKind = ref<'all' | 'texture' | 'tileset' | 'audio' | 'generated'>('all')
const prefabKinds = [
  ['all', 'All'],
  ['tile', 'Tiles'],
  ['character', 'Characters'],
  ['item', 'Items'],
  ['other', 'Other'],
] as const
const assetKinds = [
  ['all', 'All'],
  ['texture', 'Textures'],
  ['tileset', 'Tilesets'],
  ['audio', 'Audio'],
  ['generated', 'Generated'],
] as const

const visible = computed(() => {
  const query = filter.value.trim().toLowerCase()
  return assets.value.filter(
    (a) =>
      inFolder(a.path, 'assets', assetFolder.value) &&
      (assetKind.value === 'all' ||
        (assetKind.value === 'generated' ? !!a.generation : a.type === assetKind.value)) &&
      (!query ||
        a.path.toLowerCase().includes(query) ||
        (a.analysis?.description ?? '').toLowerCase().includes(query) ||
        (a.analysis?.tags ?? '').toLowerCase().includes(query) ||
        (a.analysis?.kind ?? '') === query),
  )
})
const assetFolders = computed(() => foldersOf(assets.value.map((a) => a.path), 'assets'))
const prefabFolders = computed(() => foldersOf(editorState.prefabs.map((p) => p.path), 'prefabs'))

function folderDepth(folder: string): number {
  return folder.split('/').length - 1
}
function folderLabel(folder: string): string {
  return folder.slice(folder.lastIndexOf('/') + 1)
}

// Multi-select (5.5): Ctrl+click, Shift+click, Ctrl+A, per tab.
const assetPick = ref<Selection>(emptySelection)
const prefabPick = ref<Selection>(emptySelection)
const pickedAssets = computed(() => assetPick.value.picked)
const pickedPrefabs = computed(() => prefabPick.value.picked)

// Drag a card (or every picked card) onto a folder to move it there.
const dragging = ref<string[] | null>(null)
const dropTarget = ref<string | null>(null)
function startDrag(path: string, picked: string[]): void {
  dragging.value = picked.length > 1 && picked.includes(path) ? [...picked] : [path]
}
function dropOn(folder: string, root: Root): void {
  for (const path of dragging.value ?? []) moveItem(path, root, folder)
  dragging.value = null
  dropTarget.value = null
}

onHostMessage((message) => {
  // A moved file keeps its selection.
  if (message.type === 'file:moved' && !message.error) {
    if (selectedPath.value === message.from) selectedPath.value = message.to as string
    assetPick.value = renamed(assetPick.value, message.from as string, message.to as string)
    prefabPick.value = renamed(prefabPick.value, message.from as string, message.to as string)
  }
})
const selected = computed(() => assets.value.find((a) => a.path === selectedPath.value) ?? null)

// Each asset's version, for the catalog's thumbnails.
const assetVersions = computed(() => Object.fromEntries(assets.value.map((a) => [a.path, a.version])))

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

// Everything placed in the world is a prefab, so an asset becomes placeable
// by turning it into one: a tile (painted onto cells) or a prop (stands in
// the world). The new prefab is armed as the brush right away.
const newPrefabName = ref('')
// A square image (or tile) is most likely top-down; a 2:1 one isometric.
watch(selected, (asset) => {
  if (!asset || asset.type === 'audio') return
  const w = asset.type === 'tileset' ? asset.tileWidth ?? 1 : asset.width ?? 1
  const h = asset.type === 'tileset' ? asset.tileHeight ?? 1 : asset.height ?? 1
  artFlat.value = Math.abs(w / h - 1) < 0.25
})
watch([selectedPath, selectedTile], () => {
  const stem = (selected.value?.name ?? '').replace(/\.[^.]+$/, '').replace(/[^A-Za-z0-9_-]/g, '_')
  newPrefabName.value = selected.value?.type === 'tileset' ? `${stem}_${selectedTile.value}` : stem
})

function createPrefab(asTile: boolean): void {
  sendToHost({
    type: 'prefab:createFromAsset',
    asset: selectedPath.value,
    tile: selectedTile.value,
    asTile,
    solid: paintSolid.value,
    name: newPrefabName.value,
    flat: artFlat.value,
  })
}

// Right-click menus (development_plan.md 4.1).
function prefabMenu(prefab: (typeof editorState.prefabs)[number], event: MouseEvent): void {
  if (pickedPrefabs.value.length > 1 && pickedPrefabs.value.includes(prefab.path)) {
    manyMenu('prefabs', event)
    return
  }
  prefabPick.value = { picked: [prefab.path], anchor: prefab.path }
  const items: MenuItem[] = [
    { label: prefab.isTile ? 'Paint with it' : 'Place it', disabled: editorState.playing, action: () => armPrefab(prefab.path) },
    ...(pairOf(prefab.texture) ? [{ label: pairOf(prefab.texture)!.label, action: () => goToPairedPrefab(prefab.texture) }] : []),
    separator,
    { label: 'Rename…', disabled: editorState.playing, action: () => renameItem(prefab.path) },
    { label: 'Move to folder…', disabled: editorState.playing, action: () => askMoveToFolder(prefab.path, 'prefabs') },
    { label: 'Show in Explorer', action: () => sendToHost({ type: 'asset:reveal', path: prefab.path }) },
    { label: 'Replace every copy in the scene…', disabled: editorState.playing, action: () => (editorState.replacePrefab = prefab.path) },
    separator,
    ...prefabMenuExtensions.flatMap((extend) => extend(prefab)),
  ]
  openContextMenu(event.clientX, event.clientY, items)
}

// Folder menus (5.6): rename, show, remove — keeping what's inside (it
// moves up a level) or removing it too.
function folderMenu(folder: string, which: Root, event: MouseEvent): void {
  if (!folder) return
  const full = `${which}/${folder}`
  const parent = folder.includes('/') ? folder.slice(0, folder.lastIndexOf('/')) : ''
  const paths = which === 'assets' ? assets.value.map((a) => a.path) : editorState.prefabs.map((p) => p.path)
  const count = paths.filter((p) => p.startsWith(full + '/')).length
  const noun = which === 'assets' ? (count === 1 ? 'asset' : 'assets') : count === 1 ? 'prefab' : 'prefabs'
  const items: MenuItem[] = [
    {
      label: 'Rename folder…',
      disabled: editorState.playing,
      action: async () => {
        const name = await promptDialog({
          title: 'Rename folder',
          message: `New name for ${full}. Everything in it moves, and every reference follows.`,
          value: folderLabel(folder),
          confirmLabel: 'Rename',
        })
        const clean = name?.trim().replace(/[\\/:*?"<>|]/g, '_')
        if (!clean || clean === folderLabel(folder)) return
        sendToHost({ type: 'folder:rename', folder: full, to: `${which}/${parent ? parent + '/' : ''}${clean}` })
        if (which === 'assets' ? assetFolder.value === folder : prefabFolder.value === folder) {
          if (which === 'assets') assetFolder.value = ''
          else prefabFolder.value = ''
        }
      },
    },
    { label: 'Show in Explorer', action: () => sendToHost({ type: 'asset:reveal', path: full }) },
    separator,
    {
      label: 'Remove folder…',
      danger: true,
      disabled: editorState.playing,
      action: async () => {
        const answer = await confirmWithOption({
          title: 'Remove folder',
          message: `Remove ${full}?`,
          checkbox: `Also remove the ${count} ${noun} in it (otherwise they move up to ${parent ? `${which}/${parent}` : `${which}/`})`,
          confirmLabel: 'Remove',
          danger: true,
        })
        if (!answer.confirmed) return
        sendToHost({ type: 'folder:remove', folder: full, deleteContents: answer.checked })
        if (which === 'assets') assetFolder.value = ''
        else prefabFolder.value = ''
      },
    },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}

// Several picked images (5.6): one prefab each, named after its file.
function createPrefabsForPicked(asTile: boolean): void {
  for (const path of pickedAssets.value) {
    const asset = assets.value.find((a) => a.path === path)
    if (!asset || asset.type === 'audio') continue
    const name = asset.name.replace(/\.[^.]+$/, '').replace(/[^A-Za-z0-9_-]/g, '_')
    sendToHost({ type: 'prefab:createFromAsset', asset: path, tile: 0, asTile, solid: paintSolid.value, name, flat: artFlat.value })
  }
}

// Asset analysis (6.2): what each image is, from its pixels and a vision model.
const analyzing = ref(0)
onHostMessage((message) => {
  if (message.type === 'assets:analyzing') analyzing.value = message.count as number
  else if (message.type === 'assets:analyzed') analyzing.value = 0
})

function analyze(paths: string[]): void {
  const images = paths.filter((p) => assets.value.find((a) => a.path === p)?.type !== 'audio')
  if (images.length) sendToHost({ type: 'assets:analyze', paths: images })
}

function suggestedRenames(paths: string[]): number {
  return paths.filter((p) => {
    const name = assets.value.find((a) => a.path === p)?.analysis?.name
    return !!name && !p.slice(p.lastIndexOf('/') + 1).startsWith(name + '.')
  }).length
}

async function applyNames(paths: string[]): Promise<void> {
  const count = suggestedRenames(paths)
  if (
    count &&
    (await confirmDialog({
      title: 'Rename to the suggested names',
      message: `Rename ${count} file(s) to the names the analysis suggested? Prefabs, scenes and scripts that use them are updated.`,
      confirmLabel: 'Rename',
    }))
  ) {
    sendToHost({ type: 'assets:applyNames', paths })
  }
}

function modifiers(event: MouseEvent | KeyboardEvent): { ctrl: boolean; shift: boolean } {
  return { ctrl: event.ctrlKey || event.metaKey, shift: event.shiftKey }
}

function clickAsset(asset: Asset, event: MouseEvent): void {
  assetPick.value = clickItem(assetPick.value, visible.value.map((a) => a.path), asset.path, modifiers(event))
  // The details pane shows one asset; several picked show a summary.
  selectedPath.value = assetPick.value.picked.length === 1 ? assetPick.value.picked[0]! : null
}

function clickPrefab(prefab: (typeof editorState.prefabs)[number], event: MouseEvent): void {
  const mods = modifiers(event)
  prefabPick.value = clickItem(prefabPick.value, visiblePrefabs.value.map((p) => p.path), prefab.path, mods)
  if (!mods.ctrl && !mods.shift) armPrefab(prefab.path) // a plain click still picks the brush
}

// Ctrl+A picks what's shown; Delete deletes what's picked; Esc clears.
function gridKey(event: KeyboardEvent, which: 'assets' | 'prefabs'): void {
  const shown = which === 'assets' ? visible.value.map((a) => a.path) : visiblePrefabs.value.map((p) => p.path)
  const pick = which === 'assets' ? assetPick : prefabPick
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'a') {
    event.preventDefault()
    event.stopPropagation()
    pick.value = selectAll(shown)
    if (which === 'assets') selectedPath.value = shown.length === 1 ? shown[0]! : null
  } else if (event.key === 'Delete' && pick.value.picked.length > 0) {
    event.preventDefault()
    event.stopPropagation()
    void deletePicked(which)
  } else if (event.key === 'Escape' && pick.value.picked.length > 0) {
    pick.value = emptySelection
  }
}

async function deletePicked(which: 'assets' | 'prefabs'): Promise<void> {
  const paths = which === 'assets' ? pickedAssets.value : pickedPrefabs.value
  if (paths.length === 0 || editorState.playing) return
  const list = paths.slice(0, 12).join('\n') + (paths.length > 12 ? `\n…and ${paths.length - 12} more` : '')
  const consequence =
    which === 'assets'
      ? 'This deletes the files. Prefabs that use them will show a missing texture.'
      : 'Their copies in the scene stay, as ordinary entities.'
  if (
    !(await confirmDialog({
      title: `Delete ${paths.length} ${which === 'assets' ? 'assets' : 'prefabs'}`,
      message: `${consequence}\n\n${list}`,
      confirmLabel: 'Delete',
      danger: true,
    }))
  ) {
    return
  }
  for (const path of paths) sendToHost({ type: which === 'assets' ? 'asset:delete' : 'prefab:remove', path })
  if (which === 'assets') assetPick.value = emptySelection
  else prefabPick.value = emptySelection
}

// Right-click on one of several picked cards: what to do with all of them.
function manyMenu(which: 'assets' | 'prefabs', event: MouseEvent): void {
  const paths = which === 'assets' ? pickedAssets.value : pickedPrefabs.value
  const items: MenuItem[] = [
    {
      label: `Move ${paths.length} to folder…`,
      disabled: editorState.playing,
      action: () => askMoveManyToFolder([...paths], which),
    },
    ...(which === 'assets'
      ? [{ label: `Keep ${paths.length} in my library`, action: () => sendToHost({ type: 'library:addFromProject', paths: [...paths] }) }]
      : []),
    separator,
    { label: `Delete ${paths.length}…`, danger: true, disabled: editorState.playing, action: () => deletePicked(which) },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}

// Interiors (development_plan.md Phase 27): an asset's pair, kept in its analysis both ways (interior= on the
// exterior, exteriorOf= on the interior).
function pairOf(path: string): { label: string; path: string } | null {
  const analysis = assets.value.find((a) => a.path === path)?.analysis
  if (analysis?.interior) return { label: 'Go to interior', path: analysis.interior }
  if (analysis?.exteriorOf) return { label: 'Go to exterior', path: analysis.exteriorOf }
  return null
}

async function goToAsset(path: string): Promise<void> {
  tab.value = 'assets'
  if (!inFolder(path, 'assets', assetFolder.value)) assetFolder.value = ''
  filter.value = ''
  assetPick.value = { picked: [path], anchor: path }
  selectedPath.value = path
  await nextTick()
  document.querySelector(`[data-asset="${CSS.escape(path)}"]`)?.scrollIntoView({ block: 'nearest' })
}

// A prefab's pair: the prefab drawn with its texture's pair, or that asset when no prefab draws it.
async function goToPairedPrefab(texture: string): Promise<void> {
  const pair = pairOf(texture)
  if (!pair) return
  const prefab = editorState.prefabs.find((p) => p.texture === pair.path)
  if (!prefab) return goToAsset(pair.path)
  tab.value = 'prefabs'
  if (!inFolder(prefab.path, 'prefabs', prefabFolder.value)) prefabFolder.value = ''
  prefabKind.value = 'all'
  filter.value = ''
  prefabPick.value = { picked: [prefab.path], anchor: prefab.path }
  await nextTick()
  document.querySelector(`[data-prefab="${CSS.escape(prefab.path)}"]`)?.scrollIntoView({ block: 'nearest' })
}

// The pictures that can be the selected asset's interior: images that aren't another building's exterior or interior.
const interiorChoices = computed(() =>
  assets.value.filter(
    (a) =>
      a.type !== 'audio' &&
      a.path !== selectedPath.value &&
      (!a.analysis?.interior) &&
      (!a.analysis?.exteriorOf || a.analysis.exteriorOf === selectedPath.value),
  ),
)

function setInterior(path: string, interior: string): void {
  sendToHost({ type: 'asset:setInterior', path, interior })
}

function assetMenu(asset: Asset, event: MouseEvent): void {
  if (pickedAssets.value.length > 1 && pickedAssets.value.includes(asset.path)) {
    manyMenu('assets', event)
    return
  }
  assetPick.value = { picked: [asset.path], anchor: asset.path }
  selectedPath.value = asset.path
  const items: MenuItem[] = [
    { label: 'Details', action: () => (selectedPath.value = asset.path) },
    ...(pairOf(asset.path) ? [{ label: pairOf(asset.path)!.label, action: () => goToAsset(pairOf(asset.path)!.path) }] : []),
    { label: 'Rename…', disabled: editorState.playing, action: () => renameItem(asset.path) },
    { label: 'Move to folder…', disabled: editorState.playing, action: () => askMoveToFolder(asset.path, 'assets') },
    { label: 'Show in Explorer', action: () => sendToHost({ type: 'asset:reveal', path: asset.path }) },
    { label: 'Keep in my library', action: () => sendToHost({ type: 'library:addFromProject', paths: [asset.path] }) },
    separator,
    {
      label: 'Delete…',
      danger: true,
      confirm: `Delete ${asset.path}? This deletes the file. Prefabs that use it will show a missing texture.`,
      action: () => sendToHost({ type: 'asset:delete', path: asset.path }),
    },
  ]
  openContextMenu(event.clientX, event.clientY, items)
}

// Prefabs tab: the project's palette.
// Prefabs or Assets; Scenes, World and Story are the Project panel, Level and
// Art the Generate panel (6.8).
const tab = computed({
  get: () => panelTab.assets,
  set: (value) => (panelTab.assets = value),
})
const visiblePrefabs = computed(() => {
  const query = filter.value.trim().toLowerCase()
  return editorState.prefabs.filter(
    (p) =>
      inFolder(p.path, 'prefabs', prefabFolder.value) &&
      (prefabKind.value === 'all' || p.kind === prefabKind.value) &&
      (!query || p.path.toLowerCase().includes(query)),
  )
})
// Number keys 1-9 pick from the whole palette, so the hint uses that index.
function paletteIndex(path: string): number {
  return editorState.prefabs.findIndex((p) => p.path === path)
}

watch(visible, (list) => (assetPick.value = keepShown(assetPick.value, list.map((a) => a.path))))
watch(visiblePrefabs, (list) => (prefabPick.value = keepShown(prefabPick.value, list.map((p) => p.path))))
// A thing selected in the viewport: its prefab selected here, on the Prefabs tab, shown (its folder opened up) and
// scrolled to.
watch(
  () => editorState.selectedPrefabAt,
  async () => {
    const path = editorState.selectedPrefab
    if (!path || !editorState.selectedId || !editorState.prefabs.some((p) => p.path === path)) return
    tab.value = 'prefabs'
    if (!inFolder(path, 'prefabs', prefabFolder.value)) prefabFolder.value = ''
    if (prefabKind.value !== 'all' && editorState.prefabs.find((p) => p.path === path)?.kind !== prefabKind.value) prefabKind.value = 'all'
    prefabPick.value = { picked: [path], anchor: path }
    await nextTick()
    document.querySelector(`[data-prefab="${CSS.escape(path)}"]`)?.scrollIntoView({ block: 'nearest' })
  },
)

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
  sendToHost({ type: 'prefabs:request' })
})
</script>

<template>
  <div class="asset-browser">
    <div class="header">
      <div class="tabs">
        <button :class="{ active: tab === 'prefabs' }" @click="tab = 'prefabs'">Prefabs</button>
        <button :class="{ active: tab === 'assets' }" @click="tab = 'assets'">Assets</button>
        <button :class="{ active: tab === 'web' }" title="Free art from the web (OpenGameArt, licence-checked): search, pick, add" @click="tab = 'web'">Web Search</button>
        <button :class="{ active: tab === 'library' }" title="Your own art, for every project: generated, from the web and the AIGE library, in your folders" @click="tab = 'library'">Library</button>
        <button
          :class="{ active: tab === 'catalog' }"
          title="What the builder understood about each image: role, footprint, anchor, perspective, piece sets, and what looks wrong"
          @click="tab = 'catalog'"
        >
          Catalog
        </button>
      </div>
      <input
        v-if="tab === 'prefabs' || tab === 'assets' || tab === 'catalog' || tab === 'library'"
        v-model="filter"
        class="filter"
        type="search"
        placeholder="Search"
        spellcheck="false"
      />
      <button
        v-if="tab === 'assets'"
        title="Copy images or audio into the project"
        @click="sendToHost({ type: 'asset:import' })"
      >
        Import…
      </button>
      <button
        v-if="tab === 'assets'"
        :disabled="analyzing > 0 || visible.length === 0"
        title="Work out what every image shown here is: ground tiles' border terrains from their pixels, and (with AI on) a description, kind, tags, direction, size and a better name"
        @click="analyze(visible.map((a) => a.path))"
      >
        {{ analyzing ? `Analyzing ${analyzing}…` : `Analyze ${visible.length}` }}
      </button>
      <button
        v-if="tab === 'assets'"
        :disabled="editorState.playing || visible.length === 0"
        title="Make prefabs from every image shown here, sized by their pack's scale (you review the list first)"
        @click="openPackImport(visible.filter((a) => a.type !== 'audio').map((a) => a.path))"
      >
        Import as prefabs…
      </button>
      <button
        v-if="tab === 'prefabs' || tab === 'assets'"
        title="Rescan the project folder" @click="sendToHost({ type: 'assets:refresh' })">
        Refresh
      </button>
    </div>

    <div v-if="tab === 'library'" class="body library-body"><LibraryTab :filter="filter" /></div>
    <div v-if="tab === 'web'" class="body library-body"><WebSearchTab /></div>
    <div v-if="tab === 'prefabs'" class="body">
      <nav class="folders" title="Folders inside prefabs/ — drop a card on one to move it">
        <button
          v-for="folder in ['', ...prefabFolders]"
          :key="folder"
          :class="{ active: prefabFolder === folder, drop: dropTarget === folder && dragging }"
          :style="{ paddingLeft: `${8 + (folder ? (folderDepth(folder) + 1) * 10 : 0)}px` }"
          @click="prefabFolder = folder"
          @contextmenu.prevent="folderMenu(folder, 'prefabs', $event)"
          @dragover.prevent="dropTarget = folder"
          @dragleave="dropTarget = null"
          @drop.prevent="dropOn(folder, 'prefabs')"
        >
          {{ folder ? folderLabel(folder) : 'All prefabs' }}
        </button>
      </nav>
      <div class="column">
        <div class="chips">
          <button
            v-for="[kind, label] in prefabKinds"
            :key="kind"
            :class="{ active: prefabKind === kind }"
            @click="prefabKind = kind"
          >
            {{ label }}
          </button>
        </div>
        <div class="grid" tabindex="-1" @keydown="gridKey($event, 'prefabs')">
          <p v-if="editorState.prefabs.length === 0" class="placeholder">
            No prefabs yet. Turn an asset into one from the Assets tab, or select an entity and use "Save as
            prefab" in the Inspector.
          </p>
          <p v-else-if="visiblePrefabs.length === 0" class="placeholder">Nothing here matches.</p>
          <button
            v-for="prefab in visiblePrefabs"
            :key="prefab.path"
            :data-prefab="prefab.path"
            class="card"
            :class="{
              selected: editorState.tool === 'prefab' && editorState.brush === prefab.path,
              picked: (pickedPrefabs.length > 1 && pickedPrefabs.includes(prefab.path)) || (!!editorState.selectedId && editorState.selectedPrefab === prefab.path),
              broken: !!prefab.error,
            }"
            :title="
              prefab.error ||
              `${prefab.isTile ? 'Paint' : 'Place'} ${prefab.name}${paletteIndex(prefab.path) < 9 ? ` (${paletteIndex(prefab.path) + 1})` : ''} — ${prefab.path}`
            "
            :disabled="editorState.playing"
            draggable="true"
            @click="clickPrefab(prefab, $event)"
            @dragstart="startDrag(prefab.path, pickedPrefabs)"
            @dragend="dragging = null"
            @contextmenu.prevent="prefabMenu(prefab, $event)"
          >
            <span class="thumb">
              <PrefabThumb :prefab="prefab" :box="52" />
            </span>
            <span class="name">{{ prefab.name }}</span>
            <span class="badge">
              {{ prefab.isPlayer ? 'player' : prefab.kind }}
              <template v-if="paletteIndex(prefab.path) < 9"> · {{ paletteIndex(prefab.path) + 1 }}</template>
            </span>
          </button>
        </div>
      </div>
    </div>

    <div v-else-if="tab === 'assets'" class="body">
      <nav class="folders" title="Folders inside assets/ — drop a card on one to move it">
        <button
          v-for="folder in ['', ...assetFolders]"
          :key="folder"
          :class="{ active: assetFolder === folder, drop: dropTarget === folder && dragging }"
          :style="{ paddingLeft: `${8 + (folder ? (folderDepth(folder) + 1) * 10 : 0)}px` }"
          @click="assetFolder = folder"
          @contextmenu.prevent="folderMenu(folder, 'assets', $event)"
          @dragover.prevent="dropTarget = folder"
          @dragleave="dropTarget = null"
          @drop.prevent="dropOn(folder, 'assets')"
        >
          {{ folder ? folderLabel(folder) : 'All assets' }}
        </button>
      </nav>
      <div class="column">
        <div class="chips">
          <button
            v-for="[kind, label] in assetKinds"
            :key="kind"
            :class="{ active: assetKind === kind }"
            @click="assetKind = kind"
          >
            {{ label }}
          </button>
        </div>
        <div class="grid" tabindex="-1" @keydown="gridKey($event, 'assets')">
          <p v-if="assets.length === 0" class="placeholder">
            No assets yet. Import PNG/JPG images or WAV/MP3/FLAC audio.
          </p>
          <p v-else-if="visible.length === 0" class="placeholder">Nothing here matches.</p>
          <button
            v-for="asset in visible"
            :data-asset="asset.path"
            :key="asset.path"
            class="card"
            :class="{ selected: pickedAssets.includes(asset.path), broken: !!asset.error }"
            :title="asset.error || asset.path"
            draggable="true"
            @click="clickAsset(asset, $event)"
            @dragstart="startDrag(asset.path, pickedAssets)"
            @dragend="dragging = null"
            @contextmenu.prevent="assetMenu(asset, $event)"
          >
            <span class="thumb">
              <img v-if="asset.type !== 'audio' && !asset.error" :src="urlOf(asset)" alt="" draggable="false" />
              <span v-else class="glyph">{{ asset.type === 'audio' ? '♪' : '!' }}</span>
            </span>
            <span class="name">{{ asset.name }}</span>
            <span class="badge">{{ asset.generation ? 'generated' : asset.type }}</span>
          </button>
        </div>
      </div>

      <aside v-if="pickedAssets.length > 1" class="details">
        <div class="title">{{ pickedAssets.length }} assets picked</div>
        <p class="muted small">Ctrl+click adds or removes one, Shift+click a range, Ctrl+A everything shown, Esc clears.</p>
        <div class="actions">
          <button :disabled="editorState.playing" @click="askMoveManyToFolder([...pickedAssets], 'assets')">
            Move to folder…
          </button>
          <button :disabled="editorState.playing" @click="deletePicked('assets')">Delete…</button>
        </div>
        <p class="muted small">Or drag any picked card onto a folder.</p>
        <div class="actions">
          <button :disabled="analyzing > 0" title="What each one is (6.2)" @click="analyze([...pickedAssets])">
            {{ analyzing ? 'Analyzing…' : 'Analyze' }}
          </button>
          <button
            v-if="suggestedRenames(pickedAssets) > 0"
            :disabled="editorState.playing"
            title="Rename to the names the analysis suggested; references follow"
            @click="applyNames([...pickedAssets])"
          >
            Apply {{ suggestedRenames(pickedAssets) }} suggested name(s)
          </button>
        </div>
        <div class="actions">
          <button
            class="primary"
            :disabled="editorState.playing"
            title="Prefabs for all of them, sized by their pack's scale; you review the list first"
            @click="openPackImport([...pickedAssets])"
          >
            Import {{ pickedAssets.length }} as prefabs…
          </button>
        </div>
        <div class="section-label">Make a prefab from each</div>
        <div class="row" title="Top-down art is laid onto the grid's diamond cells; isometric art is used as drawn">
          <span>Drawn</span>
          <select v-model="artFlat">
            <option :value="true">Top-down (lies flat on the grid)</option>
            <option :value="false">Isometric / upright</option>
          </select>
        </div>
        <div class="actions">
          <button title="Ground tiles you paint onto cells, named after their files" @click="createPrefabsForPicked(true)">
            New tile prefabs
          </button>
          <button title="Objects that stand in the world, named after their files" @click="createPrefabsForPicked(false)">
            New prop prefabs
          </button>
          <label class="check" title="Tiles: block movement. Props: block their cell.">
            <input v-model="paintSolid" type="checkbox" /> Solid
          </label>
        </div>
      </aside>
      <aside v-else-if="selected" class="details">
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
          <template v-if="selected.analysis">
            <div v-if="selected.analysis.description" class="row analysis">
              <span>What</span><span>{{ selected.analysis.description }}</span>
            </div>
            <div v-if="selected.analysis.kind" class="row">
              <span>Kind</span>
              <span>
                {{ selected.analysis.kind }}
                <template v-if="selected.analysis.direction && selected.analysis.direction !== 'none'">
                  · runs {{ selected.analysis.direction === 'horizontal' ? 'left–right' : 'up–down' }}
                </template>
                <template v-if="selected.analysis.blocks === '1'"> · blocks the way</template>
              </span>
            </div>
            <div v-if="selected.analysis.tags" class="row"><span>Tags</span><span>{{ selected.analysis.tags }}</span></div>
            <div v-if="selected.analysis.cells" class="row">
              <span>Game size</span><span>{{ selected.analysis.cells }} cells wide</span>
            </div>
            <div v-if="selected.analysis.edges" class="row" title="Terrain at each corner and edge, clockwise from the top-left">
              <span>Borders</span><span class="edges">{{ selected.analysis.edges }}</span>
            </div>
          </template>
          <div v-if="selected.analysis?.exteriorOf" class="row" title="This is the inside of a building">
            <span>Interior of</span>
            <span class="pair">
              <a href="#" @click.prevent="goToAsset(selected.analysis.exteriorOf)">{{ selected.analysis.exteriorOf.split('/').pop() }}</a>
              <button :disabled="editorState.playing" @click="setInterior(selected.analysis.exteriorOf, '')">Unpair</button>
            </span>
          </div>
          <div v-else class="row" title="The building's inside, shown while the player is in it (Phase 27)">
            <span>Interior</span>
            <select :value="selected.analysis?.interior ?? ''" :disabled="editorState.playing" @change="setInterior(selected.path, ($event.target as HTMLSelectElement).value)">
              <option value="">None</option>
              <option v-for="a in interiorChoices" :key="a.path" :value="a.path">{{ a.path.replace(/^assets\//, '') }}</option>
            </select>
          </div>
          <div class="actions">
            <button :disabled="analyzing > 0" title="What this image is (6.2)" @click="analyze([selected.path])">
              {{ selected.analysis ? 'Analyze again' : 'Analyze' }}
            </button>
            <button
              v-if="suggestedRenames([selected.path]) > 0"
              :disabled="editorState.playing"
              title="Rename to the suggested name; references follow"
              @click="applyNames([selected.path])"
            >
              Rename to {{ selected.analysis?.name }}
            </button>
          </div>
          <div v-if="selected.generation" class="row" :title="'Generated ' + (selected.generation.created ?? '')">
            <span>Generated</span>
            <span>
              {{ selected.generation.description }}{{ selected.generation.variant ? ' — ' + selected.generation.variant : '' }}
              <span class="muted">({{ selected.generation.model }})</span>
            </span>
          </div>
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

          <template v-if="!settingsChanged">
            <div class="section-label">Make a prefab from it</div>
            <div class="row">
              <span>Name</span>
              <input v-model="newPrefabName" class="name-input" type="text" spellcheck="false" />
            </div>
            <div class="row" title="Top-down art is laid onto the grid's diamond cells; isometric art is used as drawn">
              <span>Drawn</span>
              <select v-model="artFlat">
                <option :value="true">Top-down (lies flat on the grid)</option>
                <option :value="false">Isometric / upright</option>
              </select>
            </div>
            <div class="actions">
              <button title="A ground tile you paint onto cells" @click="createPrefab(true)">New tile prefab</button>
              <button title="An object that stands in the world (tree, crate, ...)" @click="createPrefab(false)">
                New prop prefab
              </button>
              <label class="check" title="Tiles: blocks movement. Props: blocks its cell.">
                <input v-model="paintSolid" type="checkbox" /> Solid
              </label>
            </div>
            <div class="actions">
              <button title="Use this on the selected entity's sprite" @click="applyToSelected">
                Apply to selected entity
              </button>
            </div>
          </template>
        </template>
      </aside>
    </div>
    <CatalogReview v-else class="body" :versions="assetVersions" :filter="filter" />
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
.tabs {
  display: flex;
  margin-right: 8px;
}
.tabs button {
  border-bottom-width: 2px;
}
.tabs button.active {
  border-bottom-color: #3b82f6;
  background: #262637;
}
.section-label {
  margin: 10px 0 6px;
  font-size: 0.85em;
  opacity: 0.7;
  text-transform: uppercase;
  letter-spacing: 0.04em;
}
.name-input {
  width: 150px;
}
.filter {
  width: 160px;
}
button,
input,
select {
  height: 24px;
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
.library-body {
  padding: 0 !important;
  overflow: hidden !important;
}
.body {
  flex: 1;
  display: flex;
  min-height: 0;
}
.folders {
  display: flex;
  flex-direction: column;
  width: 140px;
  flex-shrink: 0;
  overflow: auto;
  border-right: 1px solid #333;
  padding: 4px 0;
}
.folders button {
  border: none;
  border-left: 2px solid transparent;
  background: transparent;
  text-align: left;
  overflow: hidden;
  text-overflow: ellipsis;
  flex-shrink: 0;
}
.folders button.active {
  border-left-color: #3b82f6;
  background: #262637;
}
.folders button.drop {
  background: #313148;
  outline: 1px dashed #3b82f6;
}
.column {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}
.chips {
  display: flex;
  gap: 2px;
  padding: 6px 8px 0;
  flex-wrap: wrap;
}
.chips button {
  height: 20px;
  font-size: 0.85em;
  padding: 0 8px;
}
.chips button.active {
  border-color: #3b82f6;
  background: #313148;
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
.muted {
  opacity: 0.6;
}
.analysis span:last-child {
  text-align: right;
}
.edges {
  font-family: ui-monospace, Consolas, monospace;
  font-size: 0.85em;
}
.small {
  font-size: 0.85em;
}
.card.picked {
  border-color: #3b82f6;
  border-style: dashed;
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
