// Assets ▸ Import as prefabs (development_plan.md 6.3): which images the
// review window (PackImportDialog) is showing.
import { reactive } from 'vue'

export const packImport = reactive({ open: false, paths: [] as string[] })

export function openPackImport(paths: string[]): void {
  if (paths.length === 0) return
  packImport.paths = [...paths]
  packImport.open = true
}
