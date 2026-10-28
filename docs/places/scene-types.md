# Scene types

Each scene says how it's built: **Assembled** or **Paint** (docs/places/painted-places.md). The Build panel and the
Planner build a scene its own way; the project's method (Project Settings ▸ Places ▸ Method) is only the default for a
new scene.

| Type | How |
|---|---|
| Assembled | each building, tree and prop gets its own art, put together on the grid over a painted background; every piece can be moved and edited. Steps: Confirm, Layout, Plan, Assets, Result |
| Paint | the place painted whole as one picture, then marked: where one can walk and what a character goes behind; only doors, chests, items and characters get art of their own. Steps: Confirm, Layout, Plan, Paint, Mark, Assets, Result |

## Where it's set

- **New scene** (File ▸ New Scene, the Scenes panel): its name and its type.
- **The scene's menu** (right-click in the Scenes panel or the Scene Tree): **Type ▸ Assembled / Paint**.
- **The Build panel's first page**: its Build method shows the open scene's type; choosing the other sets the scene's
  type too.
- **The Planner**: the scenes it makes take the plan's method; building into a scene that has a type, that type.

Stored in the scene file's header (`# type Paint`), kept when it's saved; a scene without one is the project's method.

## Building from a picture

The Build panel's first page, **From a picture**: a picture of the place, and how it's used.

- **As the painting** (a Paint build): the picture *is* the place. No Layout, Plan or Paint: it becomes the painting
  (`places/<id>/painting.png`) over a square grid fitted to its width, and the build goes straight to **⑤ Mark**, every
  cell walkable to start with, no occluders: mark where one can't walk and what one goes behind, then Assets (doors,
  chests, items, characters) and Result.
- **As a reference**: the place is laid out and planned as usual, and ④ Paint paints it after the picture (its look and
  palette, on the plan's layout; or the picture itself, changed as the words say: "add a well", "make it winter").
  Sent to the paint call as a second image (`/places/paint` `reference`).

## Where it lives

| | |
|---|---|
| The type in the scene file | `engine/assets/SceneFiles.h/.cpp` (`SceneType`, `SetSceneType`), kept by `SaveScene` |
| Scene messages | `apps/editor/EditorApp.cpp` (`scene:new` sceneType, `scene:setType`, `ScenesJson` type) |
| The build's method from the scene | `EditorApp::MakeBuild`, `build:choices` |
| From a picture | `PlaceBuild::StartFromPicture`, `build:pickPicture`; the reference: `PlaceBuild::Paint`, `ai_service/app/bfl.py` `paint_place` |
| UI | `apps/editor/ui/src/scenes.ts`, `components/BuildPanel.vue` |
