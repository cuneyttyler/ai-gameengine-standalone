# Background images

How a scene's background (the painted ground everything stands on) is made and changed: Edit ▸ Background Image…, the
background models, painting with or without a build, using your own picture, and where the result is kept.

## What the background is

The **background** is the ground every asset stands on: roads, sidewalks, grass, plazas, stone floors. For a built
place (Assembled), it's painted by an image model from the place's plan, so every area and edge lands in its cell, with
nothing painted where the plan has rock or void, and only ground in it: no buildings, people or objects.

How it's painted: the plan's ground is drawn top-down as a flat colour map, one colour per kind of ground, with a short
description of each. The model paints the whole map once at low size (which decides every look), then, for big scenes,
paints parts again large from that picture and blends them. Code then turns the top-down picture to the scene's
isometric angle, so cells land exactly where the assets stand.

The background's colours also tone the place's sprites, so things sit in its light.

### Example: what the harbour town's background holds

You built `harbour-town` (Assembled): a quay, cobbled streets, a grassy hill and the sea as void.
1. Open the scene and hide a few buildings (or look between them): under them is one painted ground, with the quay's
   planks, the cobbles and the grass meeting where the plan put their edges.
2. Nothing is painted where the plan has the sea as void, and there are no houses, carts or people in the picture:
   those are the place's own assets standing on it.
3. The background is a warm evening orange, and the houses on it look a little warmer too: its colours tone the
   sprites.

Questions: what is the background? why doesn't the background have buildings on it? how is the background made?

## Edit ▸ Background Image…

**Edit ▸ Background Image…** opens the **Background image** dialog: "Painted again by a model from the words below: a
build's background from its plan, or, with no build, the open scene's ground behind everything."
- **Build**: **None: the open scene**, or one of the project's builds (the open one at first).
- **Model**: the background models (see "Background models").
- **Words**: what it's painted from. With a build: its background description (a first line saying what the place is
  and how it looks, then one line per kind of ground: `<ground>: <how it looks>`). With none: the open scene's last
  words.
- **Regenerate**: paints it again.
- **Use a picture file…**: your own picture instead.

Disabled while playing.

### Example: making the harbour's streets wetter

1. Choose **Edit ▸ Background Image…**.
2. **Build**: `harbour-town` (the open one). **Model**: **BFL FLUX.3 Image (keeps the map best)**.
3. Change the **Words** to:

```
A rainy harbour town at dusk, puddles everywhere, grey light.
cobbles: dark wet cobblestones with shallow puddles reflecting the sky
quay: weathered wooden planks, slick with rain, coils of rope at the edges
grass: muddy trampled grass, darker green
```

4. Press **Regenerate**. When it comes, the scene is laid with the new background.

Questions: how do I change the background? how do I repaint the ground? where is the background dialog?

## Regenerating with a build or without one

- **With a build**: that build is opened in the Build panel if it isn't, its background model and description set, and
  the background painted from its plan. When it comes, a built place whose scene is open is laid with it again;
  otherwise the editor says to open its scene and Build. In the Build panel the same background is the row at the top
  of **Build ▸ Assets** (its picture, who painted it, **Generate / Regenerate**, and its description, editable at every
  stage).
- **With no build**: the open scene's ground is painted from the words alone, in the project's view (isometric or
  top-down), saved as `assets/backgrounds/<scene>.png`, and drawn as one picture behind everything (an entity named
  "Background") over the scene's extent (its ground's cells and tiles, else 32 × 32 cells). Regenerating replaces its
  picture.
- **Use a picture file…**: with a build, a top-down picture as its ground; with none, the scene's backdrop.

### Example: a forest floor for a hand-made clearing

You made the scene `clearing` by hand, with a few tile prefabs and trees, and no build.
1. Choose **Edit ▸ Background Image…** and set **Build** to **None: the open scene**.
2. Type in **Words**: "a mossy forest floor with fallen leaves, roots and small ferns, soft dappled light".
3. Press **Regenerate**. The ground is painted in the project's view and saved as `assets/backgrounds/clearing.png`; an
   entity named `Background` draws it behind everything over the clearing's cells.
4. Prefer a picture you painted yourself? Open the dialog again and press **Use a picture file…** to pick it as the
   backdrop instead.

Questions: can I paint a background for a scene I made by hand? where is the background saved? how do I use my own
background picture?

## Background models

The models that paint backgrounds (the same list in the dialog and the Build panel):
- **BFL FLUX.3 Image (keeps the map best)**: the default and recommended.
- **BFL FLUX.2 max (as good, slower)**.
- **BFL FLUX.2 pro**.
- **BFL FLUX.2 klein (fast, loose)**: it tends to invent buildings, cars, walls and objects even when told not to, so
  the tested method uses FLUX.3 Image.

The app's default is **Preferences ▸ AI ▸ Background**; a project can choose its own in **Project Settings ▸ AI ▸
background** ("Use app default" unless set).

### Example: cars appeared in the town square's background

The town's background was painted with **BFL FLUX.2 klein (fast, loose)** and shows parked carts and a wall that the
plan doesn't have.
1. Choose **Edit ▸ Background Image…**, keep the build, and set **Model** to **BFL FLUX.3 Image (keeps the map best)**.
2. Press **Regenerate**: the new background has only ground.
3. To make FLUX.3 Image this project's choice for good, set **Project Settings ▸ AI ▸ background** to it; for every
   project, set **Preferences ▸ AI ▸ Background**.

Questions: which model paints backgrounds? why does my background have walls or cars in it? how do I change the
background model?
