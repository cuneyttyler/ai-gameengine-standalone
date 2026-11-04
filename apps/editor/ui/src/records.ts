// Record field schemas (development_plan.md 12.1), as the host sends them for
// the Records tab: engine/reflection's RecordFieldToJson.

export interface RecordFieldSpec {
  name: string
  type: string // float | int | bool | string | enum | texture | audio | script | prefab | record | object
  description?: string
  options?: string[]
  min?: number
  max?: number
  recordType?: string
  fields?: RecordFieldSpec[]
  list?: boolean
  required?: boolean
  default?: unknown
  // Only for some records: while the record's field `field` is one of `values` (a package's fields by its template).
  when?: { field: string; values: string[] }
}

// Whether a field applies to this record (`root`: the record's own values): its `when`, and an object's fields, any of
// them.
export function fieldShown(field: RecordFieldSpec, root: Record<string, unknown>): boolean {
  if (field.when && !field.when.values.includes(String(root[field.when.field] ?? ''))) return false
  if (field.type === 'object' && field.fields?.length && !field.list) return field.fields.some((sub) => fieldShown(sub, root))
  return true
}

// The record's values without the fields that don't apply to it (a template's data left from another template).
export function withoutUnused(fields: RecordFieldSpec[], data: Record<string, unknown>, root: Record<string, unknown>): Record<string, unknown> {
  const out: Record<string, unknown> = {}
  for (const field of fields) {
    if (!(field.name in data) || !fieldShown(field, root)) continue
    const value = data[field.name]
    out[field.name] = field.type === 'object' && !field.list && value && typeof value === 'object' && !Array.isArray(value)
      ? withoutUnused(field.fields ?? [], value as Record<string, unknown>, root)
      : value
  }
  return out
}

// The empty value of a field, as engine/records' DefaultValue gives it.
// `single`: one item of a list field.
export function defaultFor(field: RecordFieldSpec, single = false): unknown {
  if (field.default !== undefined && field.default !== null && !(single && field.list)) return field.default
  if (field.list && !single) return []
  switch (field.type) {
    case 'object': {
      const object: Record<string, unknown> = {}
      for (const sub of field.fields ?? []) object[sub.name] = defaultFor(sub)
      return object
    }
    case 'float':
    case 'int':
      return field.min ?? 0
    case 'bool':
      return false
    case 'enum':
      return field.options?.[0] ?? ''
    default:
      return ''
  }
}
