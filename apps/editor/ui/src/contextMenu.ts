// Right-click menus (development_plan.md 4.1): any component opens one with a
// list of items at the cursor; ContextMenuHost draws it over everything (the
// viewport included). Items that destroy something ask first.
import { reactive } from 'vue'
import { confirmDialog } from './dialogs'

export interface MenuItem {
  label: string
  action?: () => void
  shortcut?: string
  disabled?: boolean
  danger?: boolean
  // Asked before `action` runs.
  confirm?: string
  separator?: boolean
}

export const menuState = reactive({ open: false, x: 0, y: 0, items: [] as MenuItem[] })

export function openContextMenu(x: number, y: number, items: MenuItem[]): void {
  menuState.items = items
  menuState.x = x
  menuState.y = y
  menuState.open = true
}

export function closeContextMenu(): void {
  menuState.open = false
}

export async function runMenuItem(item: MenuItem): Promise<void> {
  closeContextMenu()
  if (item.disabled || !item.action) return
  if (item.confirm && !(await confirmDialog({ message: item.confirm, confirmLabel: item.label.replace(/…$/, ''), danger: item.danger }))) {
    return
  }
  item.action()
}

export const separator: MenuItem = { label: '', separator: true }
