<script setup lang="ts">
// The Assets panel's Library tab (development_plan.md 10, docs/asset_management.md): the
// user's own art, outside any project, in folders they make: generated art (kept from every
// project), art from the web and the AIGE library, and project art they keep. An item goes
// into the open project with a click; generated art can be offered to other users through
// the AIGE library (asked once, then decided per item).
import { computed, ref } from 'vue'
import { openContextMenu, separator, type MenuItem } from '../contextMenu'
import { confirmDialog, promptDialog } from '../dialogs'
import { onHostMessage, sendToHost } from '../ipc'

const LIBRARY_HOST = 'https://aige-library.local/'

interface Folder {
  id: string
  name: string
  parent: string
}
interface Item {
  id: string
  name: string
  path: string
  folder: string
  source: string
  licence?: string
  author?: string
  url?: string
  origin?: string
  created?: number
  shared?: string
  meta?: Record<string, string>
}
interface Library {
  folders: Folder[]
  items: Item[]
  sharing: string
  askSharing: boolean
  dir: string
}

const props = defineProps<{ filter: string }>()
const library = ref<Library | null>(null)
const folder = ref('') // '' : everything at the top
const selected = ref('')

onHostMessage((message) => {
  if (message.type === 'library:state') library.value = message as unknown as Library
})
sendToHost({ type: 'library:get' })

const SOURCE_NAMES: Record<string, string> = { generated: 'made by the Art tool', web: 'from the web', aige: 'from the AIGE library', project: 'from a project' }
const SHARED_NAMES: Record<string, string> = { yes: 'offered to others', no: 'private', unasked: 'not decided' }

const folders = computed(() => library.value?.folders ?? [])
const children = computed(() => folders.value.filter((f) => (f.parent ?? '') === folder.value))
const path = computed(() => {
  const out: Folder[] = []
  for (let id = folder.value; id; ) {
    const f = folders.value.find((x) => x.id === id)
    if (!f) break
    out.unshift(f)
    id = f.parent ?? ''
  }
  return out
})
const items = computed(() => {
  const s = props.filter.trim().toLowerCase()
  return (library.value?.items ?? []).filter((i) =>
    s ? i.name.toLowerCase().includes(s) || (i.meta?.tags ?? '').toLowerCase().includes(s) : (i.folder ?? '') === folder.value,
  )
})
const item = computed(() => library.value?.items.find((i) => i.id === selected.value))
const undecided = computed(() => (library.value?.items ?? []).filter((i) => i.source === 'generated' && i.shared === 'unasked').length)

async function newFolder(): Promise<void> {
  const name = await promptDialog({ title: 'New folder', message: 'Its name', value: '' })
  if (name) sendToHost({ type: 'library:newFolder', name, parent: folder.value })
}
function folderMenu(f: Folder, e: MouseEvent): void {
  openContextMenu(e.clientX, e.clientY, [
    { label: 'Open', action: () => (folder.value = f.id) },
    {
      label: 'Rename…',
      action: async () => {
        const name = await promptDialog({ title: 'Rename the folder', message: 'Its new name', value: f.name })
        if (name) sendToHost({ type: 'library:renameFolder', folder: f.id, name })
      },
    },
    { label: 'Export…', action: () => sendToHost({ type: 'library:export', folder: f.id }) },
    separator,
    {
      label: 'Delete the folder',
      danger: true,
      confirm: `Delete the folder ${f.name}? What's in it moves up a level; nothing is deleted.`,
      action: () => sendToHost({ type: 'library:deleteFolder', folder: f.id }),
    },
  ])
}
function moveTo(i: Item): MenuItem[] {
  const options: MenuItem[] = [{ label: 'The top', action: () => sendToHost({ type: 'library:move', item: i.id, folder: '' }) }]
  for (const f of folders.value) options.push({ label: f.name, action: () => sendToHost({ type: 'library:move', item: i.id, folder: f.id }) })
  return options
}
function itemMenu(i: Item, e: MouseEvent): void {
  selected.value = i.id
  const items: MenuItem[] = [
    { label: 'Add to the project', action: () => sendToHost({ type: 'library:toProject', item: i.id }) },
    { label: 'Move to', submenu: moveTo(i) },
    {
      label: 'Rename…',
      action: async () => {
        const name = await promptDialog({ title: 'Rename', message: 'Its name in your library', value: i.name })
        if (name) sendToHost({ type: 'library:rename', item: i.id, name })
      },
    },
  ]
  if (i.source === 'generated') {
    items.push(
      i.shared === 'yes'
        ? { label: 'Keep it private', action: () => sendToHost({ type: 'library:setShared', item: i.id, shared: 'no' }) }
        : { label: 'Offer it to others', action: () => sendToHost({ type: 'library:setShared', item: i.id, shared: 'yes' }) },
    )
  }
  items.push(separator, {
    label: 'Remove from the library…',
    danger: true,
    confirm: `Remove ${i.name} from your library? The file is deleted from it (projects keep their own copies).`,
    action: () => sendToHost({ type: 'library:remove', item: i.id }),
  })
  openContextMenu(e.clientX, e.clientY, items)
}
async function setSharing(choice: string): Promise<void> {
  if (
    choice === 'always' &&
    !(await confirmDialog({
      title: 'Offer your generated art to others',
      message:
        "Art the Art tool makes for you will be offered to other AIGE users through the AIGE library, credited to you. Nothing is uploaded until the AIGE library's service is online; you can make any item private before then, and change this in the Library tab at any time.",
      confirmLabel: 'Offer it',
    }))
  ) {
    return
  }
  sendToHost({ type: 'library:setSharing', choice })
}
function when(seconds?: number): string {
  return seconds ? new Date(seconds * 1000).toLocaleDateString([], { day: 'numeric', month: 'short', year: 'numeric' }) : ''
}
</script>

<template>
  <div class="library">
    <!-- Asked once: what happens to the art you generate (docs/asset_management.md §4). -->
    <section v-if="library?.askSharing" class="share-ask">
      <strong>Share the art you generate?</strong>
      <p>
        Everything the Art tool makes is kept here, for every project. You can also offer it to other AIGE users through the AIGE library
        (credited to you; nothing is uploaded until its service is online).
      </p>
      <div class="row">
        <button class="primary" @click="setSharing('always')">Offer it by default</button>
        <button @click="setSharing('ask')">I'll decide for each</button>
        <button @click="setSharing('never')">Keep it private</button>
      </div>
    </section>

    <div class="bar">
      <button class="crumb" :class="{ here: !folder }" @click="folder = ''">Library</button>
      <template v-for="f in path" :key="f.id">
        <span class="dim">/</span>
        <button class="crumb" :class="{ here: f.id === folder }" @click="folder = f.id">{{ f.name }}</button>
      </template>
      <span class="spacer"></span>
      <button title="A folder here" @click="newFolder">New folder</button>
      <button title="Add an exported library (a .aige-library folder)" @click="sendToHost({ type: 'library:import' })">Import…</button>
      <button :title="folder ? 'Export this folder' : 'Export the whole library'" @click="sendToHost({ type: 'library:export', folder })">Export…</button>
    </div>

    <div class="grid">
      <button v-for="f in props.filter ? [] : children" :key="f.id" class="card folder" @dblclick="folder = f.id" @click="folder = f.id" @contextmenu.prevent="folderMenu(f, $event)">
        <span class="icon">▤</span>
        <span class="name">{{ f.name }}</span>
      </button>
      <button
        v-for="i in items"
        :key="i.id"
        class="card"
        :class="{ picked: i.id === selected }"
        :title="i.name + ' · ' + (SOURCE_NAMES[i.source] ?? i.source)"
        draggable="false"
        @click="selected = i.id"
        @dblclick="sendToHost({ type: 'library:toProject', item: i.id })"
        @contextmenu.prevent="itemMenu(i, $event)"
      >
        <img :src="LIBRARY_HOST + i.path" alt="" loading="lazy" />
        <span class="name">{{ i.name }}</span>
        <span v-if="i.source === 'generated' && i.shared === 'yes'" class="badge" title="Offered to other users">↗</span>
      </button>
      <p v-if="!children.length && !items.length" class="dim empty">
        {{ props.filter ? 'Nothing matches.' : folder ? 'Empty: move art here from its menu.' : 'Your library is empty. Art the Art tool makes is kept here; keep project art here from the Assets tab (right-click).' }}
      </p>
    </div>

    <section v-if="item" class="details">
      <img :src="LIBRARY_HOST + item.path" alt="" />
      <div>
        <strong>{{ item.name }}</strong>
        <div class="dim">{{ SOURCE_NAMES[item.source] ?? item.source }}<span v-if="item.origin"> · in {{ item.origin }}</span> · {{ when(item.created) }}</div>
        <div v-if="item.meta?.description" class="dim">{{ item.meta.description }}</div>
        <div v-if="item.licence" class="dim">{{ item.licence }}<span v-if="item.author"> · {{ item.author }}</span></div>
        <div v-if="item.source === 'generated'" class="dim">Sharing: {{ SHARED_NAMES[item.shared ?? 'unasked'] }}</div>
        <div class="row">
          <button class="primary" @click="sendToHost({ type: 'library:toProject', item: item.id })">Add to the project</button>
          <template v-if="item.source === 'generated'">
            <button v-if="item.shared !== 'yes'" @click="sendToHost({ type: 'library:setShared', item: item.id, shared: 'yes' })">Offer it to others</button>
            <button v-if="item.shared !== 'no'" @click="sendToHost({ type: 'library:setShared', item: item.id, shared: 'no' })">Keep it private</button>
          </template>
        </div>
      </div>
    </section>
    <div class="footer dim">
      <span v-if="library">
        {{ library.items.length }} item(s) · sharing generated art:
        <select :value="library.sharing === 'unset' ? 'ask' : library.sharing" @change="setSharing(($event.target as HTMLSelectElement).value)">
          <option value="always">offer by default</option>
          <option value="ask">decide for each</option>
          <option value="never">keep private</option>
        </select>
        <span v-if="undecided"> · {{ undecided }} not decided</span>
      </span>
    </div>
  </div>
</template>

<style scoped>
.library {
  flex: 1;
  width: 100%;
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}
.share-ask {
  margin: 6px;
  padding: 8px 10px;
  border: 1px solid #3b82f6;
  background: #1a2233;
}
.share-ask p {
  margin: 4px 0 8px;
  opacity: 0.8;
}
.bar {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 4px 6px;
  border-bottom: 1px solid #262637;
}
.bar .spacer {
  flex: 1;
}
.crumb {
  border: none;
  background: transparent;
  padding: 2px 4px;
}
.crumb.here {
  font-weight: 600;
}
.grid {
  flex: 1;
  overflow: auto;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(92px, 1fr));
  gap: 6px;
  padding: 6px;
  align-content: start;
}
.card {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  padding: 4px;
  border: 1px solid #2a2a3a;
  background: #141420;
  cursor: pointer;
}
.card.picked {
  border-color: #3b82f6;
}
.card img {
  width: 76px;
  height: 76px;
  object-fit: contain;
  image-rendering: pixelated;
}
.card .icon {
  font-size: 40px;
  line-height: 76px;
  height: 76px;
  opacity: 0.6;
}
.card .name {
  font-size: 0.8em;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.badge {
  position: absolute;
  top: 2px;
  right: 4px;
  color: #a6e3a1;
}
.empty {
  grid-column: 1 / -1;
}
.details {
  display: flex;
  gap: 10px;
  padding: 6px 8px;
  border-top: 1px solid #262637;
}
.details img {
  width: 72px;
  height: 72px;
  object-fit: contain;
}
.row {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  margin-top: 4px;
}
.footer {
  padding: 4px 8px;
  border-top: 1px solid #262637;
  font-size: 0.85em;
}
.dim {
  opacity: 0.6;
}
button {
  border: 1px solid #333;
  border-radius: 0;
  background: #11111b;
  color: #eee;
  font: inherit;
  padding: 2px 8px;
  cursor: pointer;
}
button.primary {
  background: #3b82f6;
  border-color: #3b82f6;
}
</style>
