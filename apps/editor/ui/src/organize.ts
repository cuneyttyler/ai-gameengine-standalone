// Organizing prefabs and assets (development_plan.md 4.4): folders come from
// the files' paths (subfolders of prefabs/ and assets/), and Rename / Move
// ask the host to move the file, which rewrites every reference to it.
import { promptDialog } from './dialogs'
import { sendToHost } from './ipc'

export type Root = 'prefabs' | 'assets'

// "prefabs/enemies/skeleton.prefab" -> "enemies"; "" at the top.
export function folderOf(path: string, root: Root): string {
  const rest = path.startsWith(root + '/') ? path.slice(root.length + 1) : path
  const slash = rest.lastIndexOf('/')
  return slash < 0 ? '' : rest.slice(0, slash)
}

// Every folder that holds something, with its parents, sorted as a tree.
export function foldersOf(paths: string[], root: Root): string[] {
  const folders = new Set<string>()
  for (const path of paths) {
    let folder = folderOf(path, root)
    while (folder) {
      folders.add(folder)
      folder = folder.includes('/') ? folder.slice(0, folder.lastIndexOf('/')) : ''
    }
  }
  return [...folders].sort((a, b) => a.localeCompare(b))
}

// A folder shows what's in it and in its subfolders; "" shows everything.
export function inFolder(path: string, root: Root, folder: string): boolean {
  if (!folder) return true
  const own = folderOf(path, root)
  return own === folder || own.startsWith(folder + '/')
}

function fileName(path: string): { stem: string; ext: string } {
  const name = path.slice(path.lastIndexOf('/') + 1)
  const dot = name.lastIndexOf('.')
  return dot <= 0 ? { stem: name, ext: '' } : { stem: name.slice(0, dot), ext: name.slice(dot) }
}

function cleanFolder(folder: string): string {
  return folder
    .replace(/\\/g, '/')
    .split('/')
    .map((part) => part.trim())
    .filter((part) => part && part !== '.' && part !== '..')
    .join('/')
}

export function moveItem(path: string, root: Root, folder: string): void {
  const { stem, ext } = fileName(path)
  const clean = cleanFolder(folder)
  const to = `${root}/${clean ? clean + '/' : ''}${stem}${ext}`
  if (to !== path) sendToHost({ type: 'file:move', from: path, to })
}

export async function renameItem(path: string): Promise<void> {
  const { stem, ext } = fileName(path)
  const name = await promptDialog({
    title: 'Rename',
    message: `New name for ${path}. Everything that uses it (scenes, prefabs, quests, scripts) is updated.`,
    value: stem,
    confirmLabel: 'Rename',
  })
  const clean = name?.trim().replace(/[\\/:*?"<>|]/g, '_')
  if (!clean || clean === stem) return
  sendToHost({ type: 'file:move', from: path, to: `${path.slice(0, path.lastIndexOf('/') + 1)}${clean}${ext}` })
}

// Several at once (multi-select, 5.5): one question, then a move each.
export async function askMoveManyToFolder(paths: string[], root: Root): Promise<void> {
  if (paths.length === 0) return
  const folder = await promptDialog({
    title: 'Move to folder',
    message: `Folder inside ${root}/ for ${paths.length} items (empty for the top). New folders are created. References are updated.`,
    value: folderOf(paths[0]!, root),
    placeholder: 'characters',
    confirmLabel: 'Move',
  })
  if (folder !== null) for (const path of paths) moveItem(path, root, folder)
}

export async function askMoveToFolder(path: string, root: Root): Promise<void> {
  const folder = await promptDialog({
    title: 'Move to folder',
    message: `Folder inside ${root}/ (for example "characters" or "props/outdoor"; empty for the top). New folders are created. References are updated.`,
    value: folderOf(path, root),
    placeholder: 'characters',
    confirmLabel: 'Move',
  })
  if (folder !== null) moveItem(path, root, folder)
}
