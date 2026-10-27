// Crash recovery: the editor didn't close last time (a crash), and the open scene had changes newer than its file,
// written every two minutes (EditorApp::WriteRecovery). Offered back once the page is up; restored, they're unsaved
// until Save.
import { confirmDialog } from './dialogs'
import { onHostMessage, sendToHost } from './ipc'

onHostMessage(async (message) => {
  if (message.type !== 'recovery:offer') return
  const when = message.time ? new Date((message.time as number) * 1000).toLocaleString() : 'before it closed'
  const restore = await confirmDialog({
    title: 'Recover unsaved changes?',
    message: `The editor didn't close properly last time. ${message.scene as string} has changes from ${when} that weren't saved. Restore them? (They stay unsaved until you save.)`,
    confirmLabel: 'Restore them',
  })
  sendToHost({ type: restore ? 'recovery:restore' : 'recovery:discard' })
})
sendToHost({ type: 'recovery:request' })
