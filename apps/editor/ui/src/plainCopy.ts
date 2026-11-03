// A deep copy of plain data. structuredClone throws on what Vue's reactivity wraps (a proxy), and the data here is JSON.
export function plainCopy<T>(value: T): T {
  return value === undefined ? value : (JSON.parse(JSON.stringify(value)) as T)
}
