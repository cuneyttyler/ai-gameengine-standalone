# Lua modules

A project can add its own components and commands without touching C++
(development_plan.md 4.8). Each module is a folder:

```
<project>/modules/<name>/module.json   what it adds
<project>/modules/<name>/module.lua    the commands' code (optional)
```

Modules load when the editor or the player starts, before the scene, so a scene
can use their components. What they add goes into the reflection registry, which
means:

- the **Inspector** shows module components (Add Component, edit, remove);
- **scenes and prefabs** save and load them like any other component;
- **scripts** read and write them with `get_field` / `set_field`;
- the **AI** sees them in its schema and can propose them and run their commands.

Problems (bad JSON, an unknown field type, a name that already exists, a Lua
error) are listed in the console. The editor also shows the first one. The rest
of the module, and every other module, still load.

## module.json

```json
{
  "description": "What the module is for",
  "script": "module.lua",
  "components": [
    {
      "name": "Lantern",
      "description": "Shown in the Inspector and to the AI",
      "fields": [
        { "name": "radius", "type": "float", "default": 3, "min": 0.5, "max": 20, "description": "..." },
        { "name": "color", "type": "enum", "options": ["warm", "cold"], "default": "warm" }
      ]
    }
  ],
  "commands": [
    {
      "name": "lantern.setAll",
      "description": "...",
      "params": [{ "name": "lit", "type": "bool" }],
      "function": "set_all"
    }
  ]
}
```

Field types: `float`, `int`, `bool`, `string`, `enum` (needs `options`),
`texture`, `audio`, `script`, `prefab`. Command parameters can also be `entity`
or `component`. `min` and `max` are enforced everywhere: the Inspector, scripts,
commands and AI proposals. A component or command name must not clash with an
existing one.

## module.lua

Each command calls its `function` with a table of its parameters by name. The
parameters have already been checked against `module.json`.

```lua
function set_all(args)
  for _, id in ipairs(entities_with("Lantern")) do
    set_field(id, "Lantern", "lit", args.lit)
  end
end
```

Available in a module script:

| Function | Does |
| --- | --- |
| `get_field(id, component, field)` | a value, or nil |
| `set_field(id, component, field, value)` | true when set (value checked against the field) |
| `entities_with(component)` | the ids of every entity that has it |
| `log(text)` | prints to the console |

To fail a command, return `false, "why"` or raise an error. The message is shown
where the command was run.

The demo project includes a sample, `modules/lantern`: a `Lantern` component and
two commands, `lantern.setAll` and `lantern.scaleRadius`.

Not yet: per-frame module systems (use a component's `LuaScript` for behaviour),
and reloading modules without restarting the editor.
