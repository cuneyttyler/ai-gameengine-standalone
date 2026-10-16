// File ▸ Export Game… (development_plan.md 5.1): the game as a folder that
// runs without the editor (the player, renamed, plus the runtime content),
// optionally zipped. The host picks the destination and does the copying.
import { confirmDialog, infoDialog, promptDialog } from './dialogs'
import { editorState } from './editorState'
import { onHostMessage, sendToHost } from './ipc'
import { menuExtensions } from './menus'
import { projectState } from './projects'
import { watch } from 'vue'

async function exportGame(): Promise<void> {
  const name = await promptDialog({
    title: 'Export game',
    message:
      'The game’s name: the window title and the file names. The scene is saved first. Next you pick where the game folder goes.',
    value: projectState.name,
    confirmLabel: 'Next',
  })
  if (!name?.trim()) return
  const zip = await confirmDialog({
    title: 'Export game',
    message: 'Also make a .zip of the folder, ready to send?',
    confirmLabel: 'Folder and .zip',
    cancelLabel: 'Folder only',
  })
  sendToHost({ type: 'game:export', name: name.trim(), zip })
}

onHostMessage(async (message) => {
  if (message.type !== 'game:exported') return
  if (!message.ok) {
    await infoDialog({ title: 'Export failed', message: message.error as string })
    return
  }
  const folder = message.folder as string
  const zip = message.zip as string
  const build = message.build as string
  const note =
    build === 'Release' || build === 'installed'
      ? ''
      : ` It uses the ${build} player; build the Release configuration for a smaller, faster game.`
  const open = await confirmDialog({
    title: 'Game exported',
    message: `${message.files} files in ${folder}${zip ? ` and ${zip}` : ''}. Run the .exe inside to play.${note}`,
    confirmLabel: 'Show in Explorer',
    cancelLabel: 'Close',
  })
  if (open) sendToHost({ type: 'game:reveal', path: folder })
})

function updateMenu(): void {
  menuExtensions.file.afterSave = [
    ...(menuExtensions.file.afterSave ?? []).filter((item) => item.label !== 'Export Game…'),
    { label: 'Export Game…', disabled: editorState.playing, action: exportGame },
  ]
}

watch(() => [editorState.playing, projectState.recent], updateMenu, { immediate: true })
