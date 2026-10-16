// Multi-select for lists of cards (development_plan.md 5.5): click picks one,
// Ctrl+click adds or removes one, Shift+click picks the range from the last
// plain or Ctrl click, Ctrl+A picks everything shown. Pure functions over
// paths, so the rules are testable without a browser (tests/selection.test.ts).

export interface Selection {
  picked: string[] // in list order
  anchor: string | null // where Shift ranges start
}

export const emptySelection: Selection = { picked: [], anchor: null }

export function clickItem(
  selection: Selection,
  shown: string[],
  path: string,
  modifiers: { ctrl?: boolean; shift?: boolean },
): Selection {
  if (modifiers.shift && selection.anchor !== null && shown.includes(selection.anchor)) {
    const a = shown.indexOf(selection.anchor)
    const b = shown.indexOf(path)
    if (b < 0) return selection
    const [from, to] = a <= b ? [a, b] : [b, a]
    const range = shown.slice(from, to + 1)
    // Ctrl+Shift adds the range to what's picked; Shift alone replaces it.
    const base = modifiers.ctrl ? selection.picked : []
    return { picked: inOrder(shown, [...base, ...range]), anchor: selection.anchor }
  }
  if (modifiers.ctrl) {
    const picked = selection.picked.includes(path)
      ? selection.picked.filter((p) => p !== path)
      : inOrder(shown, [...selection.picked, path])
    return { picked, anchor: path }
  }
  return { picked: [path], anchor: path }
}

export function selectAll(shown: string[]): Selection {
  return { picked: [...shown], anchor: shown[0] ?? null }
}

// After the list changed (a filter, a move, a delete): what's still there.
export function keepShown(selection: Selection, shown: string[]): Selection {
  const picked = selection.picked.filter((p) => shown.includes(p))
  const anchor = selection.anchor !== null && shown.includes(selection.anchor) ? selection.anchor : picked[0] ?? null
  return { picked, anchor }
}

// After a move: the moved path is still picked, under its new name.
export function renamed(selection: Selection, from: string, to: string): Selection {
  return {
    picked: selection.picked.map((p) => (p === from ? to : p)),
    anchor: selection.anchor === from ? to : selection.anchor,
  }
}

function inOrder(shown: string[], paths: string[]): string[] {
  const set = new Set(paths)
  return shown.filter((p) => set.has(p))
}
