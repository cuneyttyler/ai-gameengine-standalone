"""Data shapes shared with the engine.

`EngineSchema` mirrors ReflectionRegistry::ExportSchema() (engine/reflection);
tests/fixtures/engine_schema.json is a snapshot the C++ test suite keeps in
sync. `EditorContext` is what the editor sends about the open project.
`Proposal` is what the service returns and the editor validates and applies.
"""

from __future__ import annotations

from typing import List, Annotated, Any, Literal, Union

from pydantic import BaseModel, ConfigDict, Field

# --- Engine schema ----------------------------------------------------------------

FieldTypeName = Literal[
    "float", "int", "bool", "string", "enum", "entity", "texture", "script", "audio", "prefab", "component"
]


class FieldSpec(BaseModel):
    name: str
    type: FieldTypeName
    description: str = ""
    options: list[str] | None = None
    min: float | None = None
    max: float | None = None
    optional: bool = False  # command parameters: may be left out
    scalable: str | None = None  # difficulty scales it, as this kind (16.1)


class ComponentSpec(BaseModel):
    name: str
    description: str = ""
    userEditable: bool = True
    dependencies: list[str] = []
    fields: list[FieldSpec] = []

    def field(self, name: str) -> FieldSpec | None:
        return next((f for f in self.fields if f.name == name), None)


class SettingsSpec(BaseModel):
    name: str
    description: str = ""
    fields: list[FieldSpec] = []

    def field(self, name: str) -> FieldSpec | None:
        return next((f for f in self.fields if f.name == name), None)


class CommandSpec(BaseModel):
    name: str
    description: str = ""
    params: list[FieldSpec] = []


class RecordFieldSpec(BaseModel):
    """A record type's field (development_plan.md 12.1): a component field type, or
    "record" (the id of a record of `recordType`) or "object" (sub-`fields`)."""

    name: str
    type: str
    description: str = ""
    options: List[str] | None = None
    min: float | None = None
    max: float | None = None
    recordType: str | None = None
    fields: List["RecordFieldSpec"] = []
    list: bool = False
    required: bool = False
    default: Any = None

    def field(self, name: str) -> "RecordFieldSpec | None":
        return next((f for f in self.fields if f.name == name), None)


class RecordTypeSpec(BaseModel):
    name: str
    description: str = ""
    fields: list[RecordFieldSpec] = []

    def field(self, name: str) -> RecordFieldSpec | None:
        return next((f for f in self.fields if f.name == name), None)


class EngineSchema(BaseModel):
    version: int
    components: list[ComponentSpec]
    settings: list[SettingsSpec] = []
    commands: list[CommandSpec] = []
    records: list[RecordTypeSpec] = []

    def record_type(self, name: str) -> RecordTypeSpec | None:
        return next((r for r in self.records if r.name == name), None)

    def component(self, name: str) -> ComponentSpec | None:
        return next((c for c in self.components if c.name == name), None)

    def settings_section(self, name: str) -> SettingsSpec | None:
        return next((s for s in self.settings if s.name == name), None)

    def command(self, name: str) -> CommandSpec | None:
        return next((c for c in self.commands if c.name == name), None)


# --- Editor context ---------------------------------------------------------------


class EntitySummary(BaseModel):
    """One non-tile entity: its id (valid for this request only), name,
    prefab, and current component values (enums by name, references as ids)."""

    id: int
    name: str = ""
    kind: str = ""
    prefab: str | None = None
    components: dict[str, dict[str, Any]] = {}
    scriptProperties: dict[str, Any] = {}


class TileSummary(BaseModel):
    """Tiles are summarized (there can be thousands): counts per prefab and the
    occupied grid bounds."""

    count: int = 0
    byPrefab: dict[str, int] = {}
    bounds: list[int] | None = None  # [minX, minY, maxX, maxY]


class PrefabSummary(BaseModel):
    path: str
    name: str
    isTile: bool = False
    components: dict[str, dict[str, Any]] = {}


class EditorContext(BaseModel):
    model_config = ConfigDict(extra="ignore")

    entities: list[EntitySummary] = []
    tiles: TileSummary = TileSummary()
    prefabs: list[PrefabSummary] = []
    assets: list[dict[str, Any]] = []  # {path, type}
    scripts: list[str] = []
    scriptSources: dict[str, str] = {}  # path -> Lua source, so existing behavior can be extended
    settings: dict[str, dict[str, Any]] = {}
    selection: int | None = None
    focus: str | None = None  # e.g. "Skeleton.Health.max" when asked from an Inspector field
    # The World Bible, compact: {key, name, summary} per entry (the service adds
    # the full text of the entries most related to the request).
    world: list[dict[str, Any]] = []
    # Dialogue and quests (docs/systems/story.md), each with its "id"; a dialogue may be
    # {"id", "speaker", "truncated": true} when the project's are too long to include.
    dialogues: list[dict[str, Any]] = []
    quests: list[dict[str, Any]] = []
    # Routines: the scene's named places ({name, x, y}) and schedules (each with "id").
    markers: list[dict[str, Any]] = []
    schedules: list[dict[str, Any]] = []
    # Scenes (development_plan.md 5.3): every scene {path, start, markers}, and the open one.
    scenes: list[dict[str, Any]] = []
    scene: str | None = None
    # Records (12.1): {type, id, record} each ("record" is left out when the
    # project's are too many to include in full).
    records: list[dict[str, Any]] = []
    # Cutscenes (15.5): {id, name, seconds} each, what play_cutscene can name.
    cutscenes: list[dict[str, Any]] = []
    # Difficulty (16): {levels: [{id, name, categories, xp}], builtIn, defaultLevel, scaling}.
    difficulty: dict[str, Any] = {}
    # Game UI (17): {themes, theme, files (the project's ui/ files), documents}.
    ui: dict[str, Any] = {}
    # Animation and effects (18): {sheets [{path, texture, clips}], particles, effects, animators, lighting}.
    fx: dict[str, Any] = {}
    # Abilities and magic (19): {abilities, effects} ids (presets and records).
    magic: dict[str, Any] = {}
    # Physics v2 (20): {layers (in use), gravity [x, y], bodies (count), triggers (names)}.
    physics: dict[str, Any] = {}
    # The story knowledge graph (21): {ontology {classes, properties, vocabularies, templates},
    # revision, nodes {id: class}, scenes [{id, order, mode, location, branch}], counts, issues}.
    storyGraph: dict[str, Any] = {}
    # The AI Planner's design (22): {title, genre, logline, stage, status}, when the project has a plan.
    plan: dict[str, Any] = {}
    # Shipping (24): {achievements (ids), accessibility {passed, total, failing [ids]}, version}.
    shipping: dict[str, Any] = {}


# --- Proposals --------------------------------------------------------------------


class SetField(BaseModel):
    op: Literal["set_field"]
    entity: int
    component: str
    field: str
    value: Any


class AddComponent(BaseModel):
    op: Literal["add_component"]
    entity: int
    component: str
    fields: dict[str, Any] = {}


class RemoveComponent(BaseModel):
    op: Literal["remove_component"]
    entity: int
    component: str


class SetPrefabField(BaseModel):
    op: Literal["set_prefab_field"]
    prefab: str
    component: str
    field: str
    value: Any


class SetSetting(BaseModel):
    op: Literal["set_setting"]
    section: str
    field: str
    value: Any


class SetScriptProperty(BaseModel):
    op: Literal["set_script_property"]
    entity: int
    name: str
    value: float | bool | str


class RunCommand(BaseModel):
    op: Literal["command"]
    name: str
    args: dict[str, Any] = {}


class WriteDialogue(BaseModel):
    """Creates or replaces dialogue/<id>.json (format: docs/systems/story.md)."""

    op: Literal["write_dialogue"]
    id: str
    dialogue: dict[str, Any]


class WriteSchedule(BaseModel):
    """Creates or replaces schedules/<id>.json (a daily routine; docs/systems/story.md)."""

    op: Literal["write_schedule"]
    id: str
    schedule: dict[str, Any]


class WriteQuest(BaseModel):
    """Creates or replaces quests/<id>.json (format: docs/systems/story.md)."""

    op: Literal["write_quest"]
    id: str
    quest: dict[str, Any]


class WriteCutscene(BaseModel):
    """Creates or replaces cutscenes/<id>.json (a timeline of tracks; docs/systems/story.md)."""

    op: Literal["write_cutscene"]
    id: str
    cutscene: dict[str, Any]


class StoryGraphOp(BaseModel):
    """Adds to the story knowledge graph (planner/kg.json; docs/systems/story-ontology.md): nodes
    (World Bible keys for characters, places, items), facts (edges, temporal ones with
    from/until scenes and a branch), facts it ends at a scene, nodes it removes."""

    op: Literal["story_graph"]
    nodes: list[dict[str, Any]] = []
    facts: list[dict[str, Any]] = []
    end: list[dict[str, Any]] = []
    remove: list[str] = []


class WriteUi(BaseModel):
    """Creates or replaces a game UI file under ui/ (a screen .rml, a style .rcss, theme.json,
    screens.json or notifications.json; docs/systems/game-ui.md)."""

    op: Literal["write_ui"]
    path: str
    content: str


class WriteAnim(BaseModel):
    """Creates or replaces an animation sheet, assets/<name>.anim.json (docs/systems/effects-animation.md)."""

    op: Literal["write_anim"]
    path: str
    sheet: dict[str, Any]


class WriteRecord(BaseModel):
    """Creates or replaces records/<type>/<id>.json (typed game data: items, ...)."""

    op: Literal["write_record"]
    type: str
    id: str
    record: dict[str, Any]


class DeleteRecord(BaseModel):
    """Deletes records/<type>/<id>.json."""

    op: Literal["delete_record"]
    type: str
    id: str


Operation = Annotated[
    Union[
        SetField, AddComponent, RemoveComponent, SetPrefabField, SetSetting, SetScriptProperty, RunCommand,
        WriteDialogue, WriteQuest, WriteSchedule, WriteCutscene, WriteUi, WriteAnim, WriteRecord, DeleteRecord,
        StoryGraphOp,
    ],
    Field(discriminator="op"),
]


class ArtRequest(BaseModel):
    """A piece of art the change needs that the project doesn't have (the Art tool makes it)."""
    name: str                      # lowercase-with-dashes; operations refer to it as "art:<name>"
    description: str               # what it is, for this scene
    kind: Literal["object", "character", "tile"] = "object"
    cells: float = 1.0             # how wide, in cells


class Proposal(BaseModel):
    summary: str
    # Art to make first: the service draws each, and "art:<name>" in the operations becomes its path.
    art: list[ArtRequest] = []
    operations: list[Operation] = []
    notes: str = ""
    # How big the request is, decided by the model (development_plan.md 2.9):
    #   small - one reviewable set of operations (most requests)
    #   task  - several steps; `operations` is the current step, `done` marks the
    #           last one; the editor applies each to a working copy, plays it and
    #           sends the results back for the next step
    #   split - too big for one go; no operations, `split` suggests smaller requests
    scope: Literal["small", "task", "split"] = "small"
    plan: list[str] = []
    done: bool = False
    split: list[str] = []
    # Set by the service, not the model: operations of a task step that still
    # didn't validate after the repair round and were left out, with why. The
    # editor reports them back so the next step can do them properly.
    skipped: list[str] = []


class Usage(BaseModel):
    inputTokens: int = 0
    outputTokens: int = 0
    requests: int = 0

    def add(self, other: "Usage") -> None:
        self.inputTokens += other.inputTokens
        self.outputTokens += other.outputTokens
        self.requests += other.requests
