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
