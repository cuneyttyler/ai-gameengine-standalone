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
  // A confirm can carry one option the user ticks (5.6: "Also remove the assets").
  checkbox: string
  checked: boolean
  // A prompt can carry one choice too (New scene: its type), with a line saying each.
  choices?: { value: string; label: string; description?: string }[]
  choice?: string
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
      checkbox: '',
      checked: false,
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
      checkbox: '',
      checked: false,
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
      checkbox: '',
      checked: false,
      resolve: () => resolve(),
    })
  })
}

// A confirmation with a checkbox: whether it was confirmed, and ticked.
export function confirmWithOption(
  options: Options & { checkbox: string; checked?: boolean },
): Promise<{ confirmed: boolean; checked: boolean }> {
  return new Promise((resolve) => {
    const request: DialogRequest = {
      kind: 'confirm',
      title: options.title ?? 'Are you sure?',
      message: options.message,
      confirmLabel: options.confirmLabel ?? 'OK',
      cancelLabel: options.cancelLabel ?? 'Cancel',
      danger: options.danger ?? false,
      value: '',
      placeholder: '',
      checkbox: options.checkbox,
      checked: options.checked ?? false,
      resolve: (r) => resolve({ confirmed: r === true, checked: request.checked }),
    }
    dialogState.queue.push(request)
  })
}

// A prompt with a choice beside it: the text and the choice, or null when cancelled.
export function promptWithChoice(
  options: Options & {
    value?: string
    placeholder?: string
    choices: { value: string; label: string; description?: string }[]
    choice?: string
  },
): Promise<{ value: string; choice: string } | null> {
  return new Promise((resolve) => {
    const request: DialogRequest = {
      kind: 'prompt',
      title: options.title ?? '',
      message: options.message,
      confirmLabel: options.confirmLabel ?? 'OK',
      cancelLabel: options.cancelLabel ?? 'Cancel',
      danger: false,
      value: options.value ?? '',
      placeholder: options.placeholder ?? '',
      checkbox: '',
      checked: false,
      choices: options.choices,
      choice: options.choice ?? options.choices[0]?.value ?? '',
      resolve: (r) => resolve(typeof r === 'string' ? { value: r, choice: request.choice ?? '' } : null),
    }
    dialogState.queue.push(request)
  })
}

export function finishDialog(result: boolean | string | null): void {
  const top = dialogState.queue.shift()
  top?.resolve(result)
}
