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

// A layout mix, shares adding up to 1 (rounded to hundredths).
export type Shares = Record<string, number>

function normalized(shares: Shares): Shares {
  const total = Object.values(shares).reduce((s, v) => s + v, 0)
  const out: Shares = {}
  const keys = Object.keys(shares)
  if (total <= 0) return out
  let left = 1
  keys.forEach((k, i) => {
    const v = i === keys.length - 1 ? Math.round(left * 100) / 100 : Math.round(((shares[k] ?? 0) / total) * 100) / 100
    out[k] = v
    left -= v
  })
  return out
}

// Turns a pattern on (an equal share, the others making room) or off (the
// others growing back to 1). The last pattern can't be turned off.
export function toggleShare(shares: Shares, key: string): Shares {
  const keys = Object.keys(shares)
  if (key in shares) {
    if (keys.length === 1) return { ...shares }
    const rest = { ...shares }
    delete rest[key]
    return normalized(rest)
  }
  const n = keys.length + 1
  const out: Shares = {}
  for (const k of keys) out[k] = (shares[k] ?? 0) * (1 - 1 / n)
  out[key] = 1 / n
  return normalized(out)
}

// Sets one pattern's share (0-1); the others share what's left in proportion.
export function setShare(shares: Shares, key: string, share: number): Shares {
  const s = Math.min(1, Math.max(0, share))
  const others = Object.keys(shares).filter((k) => k !== key)
  if (others.length === 0) return { [key]: 1 }
  const total = others.reduce((sum, k) => sum + (shares[k] ?? 0), 0)
  const out: Shares = { [key]: s }
  for (const k of others) out[k] = total > 0 ? ((shares[k] ?? 0) / total) * (1 - s) : (1 - s) / others.length
  return normalized(Object.fromEntries(Object.entries(out).filter(([, v]) => v > 0)))
}
