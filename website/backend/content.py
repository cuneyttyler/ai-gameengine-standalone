"""Everything the site says, in one place: the Vue app renders it from /api/content.

Facts come from the repo: docs/manuals (User Guide, Builder, AI Planner), docs/ai/agent.md, docs/ai/ai-planner.md,
docs/places/*.md, ontology/README.md, docs/systems/asset_management.md and docs/project/roadmap.md.
"""

CONTENT = {
    "hero": {
        "eyebrow": "AIGE · AI Game Engine",
        "title": "Make your game in weeks, not years.",
        "lead": (
            "AIGE is a game engine and editor for 2D isometric and top-down games with AI built into it. "
            "Make everything by hand, ask the AI to make any part of it, or mix the two: what the AI makes are "
            "the same scenes, prefabs and records you edit yourself."
        ),
        "image": "/img/village.webp",
        "caption": "A river village made in AIGE: placed, painted and dressed by the Builder.",
        "stats": [
            {"value": "13", "label": "Planner stages, idea to playable game"},
            {"value": "236", "label": "GenBuilding ontology modules"},
            {"value": "11", "label": "build types, from farmland to dungeons"},
            {"value": "100%", "label": "usable with AI turned off"},
        ],
    },
    "pillars": [
        {
            "title": "AI Panel",
            "text": "Almost anything you do by hand, the AI panel does from words: it reads what it needs, plans a big "
                    "request as steps, carries them out on a working copy and shows you the result to review.",
            "anchor": "ai-panel",
        },
        {
            "title": "AI Planner",
            "text": "Describe a game in a sentence or a page. The Planner designs the concept, story, characters, quests, "
                    "places, mechanics and interface, then builds them into real scenes.",
            "anchor": "planner",
        },
        {
            "title": "Builder",
            "text": "A whole place from a description: a village by a river, a walled town, a cellar, a dungeon. The AI "
                    "decides what it contains; solvers decide where things go.",
            "anchor": "builder",
        },
        {
            "title": "Hand-made, first class",
            "text": "Every tool a hand-built engine offers: scenes, prefabs, an Inspector, animation, story, items, "
                    "combat, UI, Lua. Manual authoring is never a second-class path.",
            "anchor": "manual",
        },
    ],
    "aiPanel": {
        "title": "The AI panel does what you'd do by hand",
        "lead": (
            "Ask in plain words. A small request comes back as a list of changes to review; a big one becomes a task "
            "carried out step by step on a working copy, played, checked and reviewed as a whole at the end."
        ),
        "capabilities": [
            {"title": "Generate prefabs", "text": "Art drawn for exactly what the scene needs, made into prefabs: every copy follows."},
            {"title": "Characters & animation", "text": "Characters drawn from 4 or 8 directions, then animated: walk, attack, a lid that swings open."},
            {"title": "Build places", "text": "Ground, roads, buildings and props, or the request handed to the Builder with everything filled in."},
            {"title": "Stories & people", "text": "A village's people, their dialogue and gossip, quests and lore. It opens with an outline you approve first."},
            {"title": "Gameplay", "text": "Input actions, Reactions, moving parts, Character Rules, records and Lua scripts, written and attached."},
            {"title": "Looks before it acts", "text": "It finds entities and art, reads files, checks problems, looks at a picture of the scene and playtests it."},
        ],
        "kinds": [
            {"name": "Small change", "text": "A reviewed list of operations."},
            {"name": "Task", "text": "Up to 20 steps on a working copy, each applied, played and sent back."},
            {"name": "Outline first", "text": "Stories and people: approve or change the outline before anything is made."},
            {"name": "Ask", "text": "One question with a few answers to click when it really needs you."},
        ],
        "safety": [
            "Every proposal is validated against the engine's own rules",
            "Canon, dialogue and quests checked against the World Bible",
            "Undo puts back every file it wrote",
            "AI is opt-in: one switch turns it off",
        ],
    },
    "planner": {
        "title": "AI Planner: from one idea to a playable game",
        "lead": (
            "One request, such as \"a short dark-fantasy action RPG about a disgraced warden recovering a stolen amulet, "
            "three towns and a dungeon\", becomes a game. Every stage writes a document you can read, edit, lock or "
            "regenerate, and you decide how much of it to check."
        ),
        "groups": [
            {"name": "Design", "range": "1–9"},
            {"name": "Build", "range": "10–13"},
        ],
        "steps": [
            {"n": 1, "name": "Concept", "group": "Design",
             "text": "Your request is read three ways (literally, typically and generously) and merged into a Game Brief. "
                     "Where the readings differ or had to guess, it says so. You confirm it in tabs: Game, Story, "
                     "Characters, Places, Systems, Difficulty, Visuals, Plan."},
            {"n": 2, "name": "Outline", "group": "Design",
             "text": "The story outline, cast and places drafted together in one context, with a world graph of how the "
                     "places connect, so everything fits from the start."},
            {"n": 3, "name": "Story", "group": "Design",
             "text": "Acts become chapters and scene cards, each written against the story's knowledge graph and checked by "
                     "continuity rules. A critic reviews every act."},
            {"n": 4, "name": "Characters", "group": "Design",
             "text": "Who they are, their voice and their daily life: biography, relationships, class and behaviour "
                     "packages for everyone, worked on in parallel."},
            {"n": 5, "name": "Quests", "group": "Design",
             "text": "The main quest and side quests with stages, objectives and rewards. Side quests grow out of what the "
                     "characters want."},
            {"n": 6, "name": "Places", "group": "Design",
             "text": "Each place described from what the story, characters and quests need, with a brief ready for the "
                     "Builder."},
            {"n": 7, "name": "Systems", "group": "Design",
             "text": "Mechanics, data and behaviour: the gameplay features, a design for each system, items, recipes, loot "
                     "and enemies, and who does what, when."},
            {"n": 8, "name": "Visuals", "group": "Design",
             "text": "The interface: theme, menus, windows, HUD and notifications."},
            {"n": 9, "name": "Validate", "group": "Design",
             "text": "Does it all fit together? Story rules, cross-document checks, an event map and an AI critic produce a "
                     "report with fixes. With no errors left, the design becomes the baseline."},
            {"n": 10, "name": "Build places", "group": "Build",
             "text": "Each place goes through the Builder, automatically or waiting for you at each step."},
            {"n": 11, "name": "Populate", "group": "Build",
             "text": "Characters go to their homes, enemies are created, dialogue and behaviour attached, and quest items "
                     "placed in the built world."},
            {"n": 12, "name": "Write & playtest", "group": "Build",
             "text": "Dialogue is written last, when every scene, voice and quest is fixed. Then an automated playtest: "
                     "every scene loads, every gateway leads somewhere, every objective can be reached."},
            {"n": 13, "name": "Result", "group": "Build",
             "text": "The game's page: what you asked for and what was made, its places, quests and checks, and Play from "
                     "the start."},
        ],
        "controls": [
            {"name": "Confirm · Review · Auto", "text": "Choose for each stage how much it waits for you."},
            {"name": "Why? · Edit · Lock · Regenerate", "text": "On every item the Planner made."},
            {"name": "Regenerate stale", "text": "Redoes exactly the documents whose inputs changed."},
            {"name": "Resumable", "text": "Jobs are saved; every AI answer is recorded as a decision."},
        ],
        "genres": ["Action RPG", "Top-down RPG", "Action-adventure", "Life-sim"],
    },
    "builder": {
        "title": "The Builder: a whole place from a description",
        "lead": (
            "The AI decides what a place contains and how its parts relate; seeded solvers decide where things go. "
            "The result follows real rules, goes into your scene as one undo step, and stays editable."
        ),
        "steps": [
            {"key": "confirm", "name": "Confirm", "methods": ["assembled", "paint"],
             "text": "Your request becomes a Brief you confirm: what kind of place, its size, what it contains, its style. "
                     "What the AI wasn't sure of is listed with the answer it assumed, so you only check those."},
            {"key": "layout", "name": "Layout", "methods": ["assembled", "paint"],
             "text": "The place is planned as a structure of parts, then solved into real positions: zones, roads, "
                     "buildings. Revise it in words (\"move the market next to the river\") and only that part changes."},
            {"key": "plan", "name": "Plan", "methods": ["assembled", "paint"],
             "text": "The detailed plan of what goes where. Revise it in words or by hand: paint a zone exactly where you "
                     "want it and it takes those cells."},
            {"key": "paint", "name": "Paint", "methods": ["paint"],
             "text": "The whole place is painted from the plan as one picture."},
            {"key": "mark", "name": "Mark", "methods": ["paint"],
             "text": "Over the painting, mark where one can walk and what a character goes behind. The plan fills this in; "
                     "click a building to select it and refine."},
            {"key": "assets", "name": "Assets", "methods": ["assembled", "paint"],
             "text": "For each thing the place needs: your project's art if it fits, your library, the web or the Art "
                     "tool, made as a matched set so the pieces look like one world. Doors are animated so buildings open."},
            {"key": "result", "name": "Result", "methods": ["assembled", "paint"],
             "text": "The place goes into your scene and is checked: every door can be reached, nothing overlaps, the art "
                     "is in place. What fails is repaired where it can be."},
        ],
        "after": [
            {"name": "Revise one part", "text": "\"Add a well to the square\": only that part is redone."},
            {"name": "Lock", "text": "Parts you edited by hand stay as they are."},
            {"name": "Undo", "text": "The whole build is one step."},
            {"name": "From your picture", "text": "Use your own painting as the place, or as a reference for one."},
        ],
        "buildTypes": ["Settlement", "Farmland", "Wilderness", "Camp", "Harbour", "Castle", "Region", "Dungeon",
                       "Interior", "Courtyard", "Modern city"],
    },
    "methods": {
        "title": "Two ways to build a place",
        "lead": "Choose per project, per scene or per plan. Both start from the same Brief, Layout and Plan.",
        "items": [
            {
                "key": "assembled",
                "name": "Assembled",
                "tagline": "Every piece its own",
                "text": "Each building, tree and prop gets its own art, put together on the grid over a painted "
                        "background. Every piece can be moved and edited.",
                "bestFor": "Places you'll keep rearranging",
                "art": "Found in your project or library, or made",
                "steps": "Confirm → Layout → Plan → Assets → Result",
            },
            {
                "key": "paint",
                "name": "Paint",
                "tagline": "One rich picture",
                "text": "The place is painted whole as one picture, then you mark where one can walk and what a character "
                        "goes behind. Only doors, chests, items and characters get art of their own.",
                "bestFor": "A rich, unified look",
                "art": "The painting holds the art; it's never cut up",
                "steps": "Confirm → Layout → Plan → Paint → Mark → Assets → Result",
            },
        ],
    },
    "ontology": {
        "title": "GenBuilding Ontology: places that follow rules",
        "lead": (
            "GBO is the knowledge the Builder generates places from: the parts a place is made of, what each may contain "
            "and how typically, how parts relate in space, what art can fill each role, and design rules, each with its "
            "source."
        ),
        "pipeline": [
            {"name": "Brief", "text": "Your request, typed and confirmed"},
            {"name": "Spec tree", "text": "The plan as a tree of GBO parts"},
            {"name": "Layout program", "text": "Relations in the Place Layout Language"},
            {"name": "Seeded solvers", "text": "Cells and entities, deterministically"},
            {"name": "Verify & repair", "text": "Each level checked before the next"},
        ],
        "points": [
            {"title": "Deterministic", "text": "The AI decides what and where in words and relations; seeded code decides "
                                               "which cells. The same document and seed always give the same place."},
            {"title": "Validated", "text": "Every AI answer is checked against the ontology before it's used, and every "
                                           "step has a fallback that needs no AI."},
            {"title": "Scales", "text": "The model plans only the upper levels; the ontology's expansion rules fill the "
                                        "rest, so a city takes tens of model calls, not thousands."},
            {"title": "Explainable", "text": "Ask why something was placed where it is, and get the rule that put it there."},
        ],
        "extend": [
            {"name": "Core", "text": "Written by hand, published as OWL 2 / SHACL"},
            {"name": "Reviewed AI proposals", "text": "New parts suggested by the AI, accepted by people"},
            {"name": "Learned", "text": "Weights learned from verified builds"},
            {"name": "Your project", "text": "Its own parts, in the project's places folder"},
            {"name": "You", "text": "File ▸ Ontology: new parts, fields and children, exported as one file"},
        ],
        "story": "Its sibling, the Game Story Ontology, gives the AI Planner the same footing for stories: classes, "
                 "properties, templates and rules the story is checked against.",
    },
    "manual": {
        "title": "Everything by hand, too",
        "lead": "The editor is fully usable with AI turned off. Hand edits and AI edits work on exactly the same files.",
        "groups": [
            {"name": "Scenes & places", "items": ["Isometric or top-down grids", "Autotiling ground, raised ground, cliffs and ramps",
                                                  "Multiple scenes joined by gateways", "Buildings with floors and cutaway or scene interiors"]},
            {"name": "Prefabs & assets", "items": ["Prefabs with per-copy overrides", "A reflection-generated Inspector",
                                                   "Asset pack import", "Assets, Characters, Animations and Library tabs"]},
            {"name": "Characters & animation", "items": ["Character sheets: every direction, every action", "Animate a part of a picture: doors, levers, fires",
                                                         "Particles, light, screen shake and slow motion", "Cutscenes"]},
            {"name": "Gameplay", "items": ["Input actions for keyboard, mouse and gamepad", "Reactions: triggers, requirements and results, no code",
                                           "Items, inventory, equipment and crafting", "Combat, magic, perks, factions and difficulty"]},
            {"name": "Characters' minds", "items": ["Daily routines and behaviour packages", "Sight and sound perception",
                                                    "Character Rules", "Speech and barks"]},
            {"name": "Story", "items": ["World Bible", "Dialogue graphs with choices, conditions and skill checks",
                                        "Quests with stages and ten kinds of objective", "Story graph"]},
            {"name": "Interface", "items": ["Visual UI designer: anchors, nine-slice, data binding", "Title, pause, inventory, shop, journal, HUD screens",
                                            "Notifications, speech bubbles, floating numbers"]},
            {"name": "Code, play & ship", "items": ["Lua scripting with errors in the Inspector", "Play in the editor, standalone player",
                                                    "Saves, audio, camera, localisation, physics", "File ▸ Export Game"]},
        ],
    },
    "library": {
        "title": "The AIGE library",
        "lead": (
            "Your art travels with you: a library outside every project, organised as you like, used by any project. "
            "And soon, a shared library where AIGE users offer the art they generate and find art that fits their world."
        ),
        "features": [
            {"title": "Your library", "text": "Generated art from every project, web art and kept project art in folders "
                                              "you make. One click puts it into a project."},
            {"title": "Share what you make", "text": "Offer the art you generate to other AIGE users, credited to you, with "
                                                     "the prompt that made it. Reviewed for licence and content."},
            {"title": "Find what fits", "text": "Assets are matched to what a place needs by role, footprint, perspective "
                                                "and style, scored for fit, not picked by file name."},
            {"title": "Style families", "text": "A curated, pre-analysed collection organised in style families, with "
                                                "licences recorded, installable per project."},
        ],
        "order": ["Your project", "Your library", "AIGE library", "The web (if you allow it)", "Generate", "A labelled placeholder"],
        "note": "The shared AIGE library opens with its online service. Nothing is uploaded before then, and there's no telemetry.",
    },
    "speed": {
        "title": "Weeks, not months or years",
        "lead": (
            "The slow parts of making a game are writing it down, building every place, drawing every character and "
            "wiring it all together. AIGE does those with you, and you keep the parts that make the game yours."
        ),
        "rows": [
            {"task": "Design document, story, cast, quests", "before": "Weeks of writing", "after": "Planner stages 1–9, reviewed by you"},
            {"task": "A town with its houses and props", "before": "Days of placing tiles", "after": "One Builder run, revised in words"},
            {"task": "A character in 8 directions, animated", "before": "Days per character", "after": "Generated, then adjusted"},
            {"task": "Thirty villagers with dialogue", "before": "A week of writing", "after": "An outline you approve, then made"},
            {"task": "Checking nothing is broken", "before": "Manual playthroughs", "after": "Automated playtest every build"},
        ],
    },
    "roadmap": {
        "title": "Roadmap: v1.0 to v2.0",
        "lead": "Version 1.0 makes 2D isometric and top-down games with AI. Each later version adds genres in the order games appeared, then 3D, then advanced graphics.",
        "versions": [
            {"v": "1.0", "name": "2D isometric and top-down", "era": "1986–2000", "status": "current",
             "text": "Action RPGs, top-down RPGs, action-adventure and life-sims, made by hand or by the Planner, exported and played through. Windows, macOS and Linux."},
            {"v": "1.1", "name": "Single-screen arcade and puzzle", "era": "1972–1985", "text": "A rules-first Planner for games without a story."},
            {"v": "1.2", "name": "Scrolling 2D", "era": "1981–1991", "text": "Shooters, platformers, beat 'em ups; side view."},
            {"v": "1.3", "name": "Exploration and physics in 2D", "era": "1986–1997", "text": "Exploration platformers and physics games."},
            {"v": "1.4", "name": "Turns, parties and stories", "era": "1986–1998", "text": "Turn-based and party RPGs. Mobile export."},
            {"v": "1.5", "name": "Strategy and simulation", "era": "1989–2000", "text": "Strategy and sims, with lockstep multiplayer."},
            {"v": "1.6", "name": "Pseudo-3D", "era": "1982–1995", "text": "Raycast and scaled-sprite worlds."},
            {"v": "1.7", "name": "True 3D", "era": "1996–2000", "text": "A 3D renderer, 3D physics and online multiplayer."},
            {"v": "1.8", "name": "3D isometric and 3D RPGs", "era": "2000–2005", "text": "The isometric RPG, now in 3D."},
            {"v": "1.9", "name": "Open worlds", "era": "2001–2005", "text": "Open worlds of the early 2000s."},
            {"v": "2.0", "name": "The finished engine", "era": "Modern", "status": "goal",
             "text": "A complete engine on the scale of today's, starting with advanced graphics. Web and consoles."},
        ],
        "everyVersion": ["AI editing", "A Planner genre profile", "Generators", "AI assets", "A playtest bot", "An off switch"],
    },
    "signup": {
        "title": "Make your first place today",
        "lead": "Leave your email and we'll tell you when AIGE 1.0 is ready to download.",
    },
}
