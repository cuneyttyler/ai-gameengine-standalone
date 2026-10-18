// Small pieces of the Build panel's confirmation window (development_plan.md
// 7.4) that don't need Vue: reading and writing ranges, and the layout
// sketches.

// A count as the Brief holds it (n, [lo, hi] or {count: ...}) as [lo, hi].
export function countRange(value: unknown): [number, number] {
  const v = value && typeof value === 'object' && !Array.isArray(value) && 'count' in value ? (value as { count: unknown }).count : value
  if (typeof v === 'number') return [v, v]
  if (Array.isArray(v) && v.length === 2 && typeof v[0] === 'number' && typeof v[1] === 'number') return [v[0], v[1]]
  return [0, 0]
}

// "3", "3-5", "3–5", "3 to 5" -> [3, 3] / [3, 5]; anything else null.
export function parseRange(text: string): [number, number] | null {
  const match = text.trim().match(/^(\d+(?:\.\d+)?)\s*(?:(?:-|–|to)\s*(\d+(?:\.\d+)?))?$/)
  if (!match) return null
  const lo = Number(match[1])
  const hi = match[2] === undefined ? lo : Number(match[2])
  return lo <= hi ? [lo, hi] : [hi, lo]
}

// An area or count for reading: "3600–10000".
export function amountText(value: unknown): string {
  const v = value && typeof value === 'object' && !Array.isArray(value) && 'area' in value ? (value as { area: unknown }).area : value
  if (typeof v === 'number') return String(v)
  if (Array.isArray(v) && v.length === 2) return v[0] === v[1] ? String(v[0]) : `${v[0]}–${v[1]}`
  return '—'
}

// A 40x28 sketch of each settlement layout, as SVG shapes.
export function layoutSketch(layout: string): string {
  switch (layout) {
    case 'grid':
      return '<path d="M4 6H36M4 14H36M4 22H36M10 3V25M20 3V25M30 3V25"/>'
    case 'radial':
      return '<circle cx="20" cy="14" r="3"/><path d="M20 11V2M20 17V26M17 14H6M23 14H34M18 12L10 5M22 12L30 5M18 16L10 23M22 16L30 23"/>'
    case 'linear-along-road':
      return '<path d="M2 14H38"/><rect x="5" y="7" width="5" height="4"/><rect x="14" y="7" width="5" height="4"/><rect x="23" y="7" width="5" height="4"/><rect x="9" y="17" width="5" height="4"/><rect x="19" y="17" width="5" height="4"/><rect x="29" y="17" width="5" height="4"/>'
    case 'linear-along-river':
      return '<path d="M2 18C10 14 16 22 24 17S34 14 38 16" stroke-width="2.5"/><rect x="6" y="6" width="5" height="4"/><rect x="16" y="5" width="5" height="4"/><rect x="27" y="6" width="5" height="4"/>'
    case 'growth':
      return '<circle cx="20" cy="14" r="4"/><circle cx="20" cy="14" r="8" stroke-dasharray="2 2"/><circle cx="20" cy="14" r="12" stroke-dasharray="1 3"/>'
    default: // organic
      return '<path d="M4 20C10 12 14 18 20 10S30 8 36 14"/><path d="M12 25C14 18 20 20 22 13"/><rect x="7" y="8" width="4" height="4"/><rect x="25" y="17" width="5" height="4"/><rect x="15" y="3" width="4" height="4"/>'
  }
}
