// How the AI builds places (Project Settings ▸ Places ▸ method; engine/gameplay/Settings.h): Assembled, Paint (Free)
// ("Paint") or Paint (by Plan) ("PaintPlan"). New Project, Preferences, the Planner and the Build panel choose from these.
export type BuildMethod = 'Assembled' | 'Paint' | 'PaintPlan'

export const BUILD_METHODS: { id: BuildMethod; label: string; short: string; title: string }[] = [
  {
    id: 'Assembled',
    label: 'Assembled',
    short: "each piece's art",
    title: 'Each building, tree and prop gets its own art, put together on the grid over a painted background; every piece can be moved and edited. Steps: Confirm, Layout, Plan, Assets, Build.',
  },
  {
    id: 'Paint',
    label: 'Paint (Free)',
    short: 'painted freely from its zones, no Plan step',
    title: "Each place is painted whole as one picture, freely: its zones are laid out, each zone's ground is coloured on the land (roads, water and woods as they are), and the painter fills each colour with its part of the place. There's no Plan step. Then you mark where one can walk and what a character goes behind; only doors, chests, items and characters get art of their own. Steps: Confirm, Layout, Paint, Mark, Assets, Build.",
  },
  {
    id: 'PaintPlan',
    label: 'Paint (by Plan)',
    short: 'the plan drawn as a block-out, then painted',
    title: "Each place is planned in full (every building, garden and road), the plan is drawn as a block-out and the block-out is painted whole as one picture. Then you mark where one can walk and what a character goes behind, pre-filled from the plan; only doors, chests, items and characters get art of their own. Steps: Confirm, Layout, Plan, Paint, Mark, Assets, Build.",
  },
]

// Whether a method paints its places (either Paint).
export function isPaintMethod(method: string | undefined | null): boolean {
  return method === 'Paint' || method === 'PaintPlan'
}

// A method as stored ("PaintFree" read as "Paint"; anything else Assembled).
export function normalBuildMethod(method: string | undefined | null): BuildMethod {
  if (method === 'Paint' || method === 'PaintFree') return 'Paint'
  if (method === 'PaintPlan') return 'PaintPlan'
  return 'Assembled'
}

export function buildMethodLabel(method: string | undefined | null): string {
  return BUILD_METHODS.find((m) => m.id === normalBuildMethod(method))?.label ?? 'Assembled'
}
