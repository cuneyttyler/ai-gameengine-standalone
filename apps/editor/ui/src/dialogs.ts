// In-app dialogs (development_plan.md 4.1). The browser's own confirm() opens
// a native box that can end up behind the viewport and lock the editor (a bug
// from testing: deleting a quest); these are part of the page instead, and
// return promises so callers read like the old confirm().
import { reactive } from 'vue'

interface DialogRequest {
  kind: 'confirm' | 'prompt' | 'info'
  title: string
  message: string
  confirmLabel: string
  cancelLabel: string
  danger: boolean
  value: string
  placeholder: string
  resolve: (result: boolean | string | null) => void
}

export const dialogState = reactive({ queue: [] as DialogRequest[] })

interface Options {
  title?: string
  message: string
  confirmLabel?: string
  cancelLabel?: string
  danger?: boolean
}

export function confirmDialog(options: Options): Promise<boolean> {
  return new Promise((resolve) => {
    dialogState.queue.push({
      kind: 'confirm',
      title: options.title ?? 'Are you sure?',
      message: options.message,
      confirmLabel: options.confirmLabel ?? 'OK',
      cancelLabel: options.cancelLabel ?? 'Cancel',
      danger: options.danger ?? false,
      value: '',
      placeholder: '',
      resolve: (r) => resolve(r === true),
    })
  })
}

export function promptDialog(options: Options & { value?: string; placeholder?: string }): Promise<string | null> {
  return new Promise((resolve) => {
    dialogState.queue.push({
      kind: 'prompt',
      title: options.title ?? '',
      message: options.message,
      confirmLabel: options.confirmLabel ?? 'OK',
      cancelLabel: options.cancelLabel ?? 'Cancel',
      danger: false,
      value: options.value ?? '',
      placeholder: options.placeholder ?? '',
      resolve: (r) => resolve(typeof r === 'string' ? r : null),
    })
  })
}

export function infoDialog(options: Options): Promise<void> {
  return new Promise((resolve) => {
    dialogState.queue.push({
      kind: 'info',
      title: options.title ?? '',
      message: options.message,
      confirmLabel: options.confirmLabel ?? 'Close',
      cancelLabel: '',
      danger: false,
      value: '',
      placeholder: '',
      resolve: () => resolve(),
    })
  })
}

export function finishDialog(result: boolean | string | null): void {
  const top = dialogState.queue.shift()
  top?.resolve(result)
}
