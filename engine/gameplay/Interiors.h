#pragma once

#include "ecs/Registry.h"

#include <optional>
#include <vector>
#include <utility>
#include <string>

namespace aige {

struct Interior;
struct AnimatedPart;

// Interiors (development_plan.md Phase 27): a building with an Interior shows it while the player stands inside its
// footprint, its exterior otherwise.

// Each Interior's `shown` from where the player stands (Play), then the pictures as `shown` says.
void UpdateInteriors(Registry& registry, std::optional<Entity> player);
// The cell of floor change `change` (1: floor 1 to 2) of a building whose footprint is [x0, x1] x [y0, y1]: its
// Interior.stairs' listed cell, else its corners in turn (top, right, left).
std::pair<int, int> StairsCell(const Interior& interior, int change, int x0, int y0, int x1, int y1);
// The pictures as each Interior's `shown` says (the interior's texture, or the exterior put back): every frame in the
// editor too, so the Inspector's shown previews it.
void ApplyInteriors(Registry& registry);
// Its footprint in world cells, inclusive: `cells` when set (a painted building), else its Obstacle's around its cell.
struct Interior;
bool InteriorFootprint(Registry& registry, Entity entity, const Interior& interior, int& x0, int& y0, int& x1, int& y1);

// How many floors it shows (1 + its upper floors' pictures), and floor k's picture (1: `texture`).
int InteriorFloors(const Interior& interior);
std::string InteriorPicture(const Interior& interior, int floor);
// A scene interior's doorway cell (world cells), where stepping in goes to its scene; false without a footprint.
bool InteriorDoor(Registry& registry, Entity entity, const Interior& interior, int& x, int& y);

// How a thing's animated door opens (docs/assets/animations.md §4.2): "auto" or "use", its own or the project's.
std::string DoorOpens(const AnimatedPart& part);
// Its doors (§4.3): itself (its first) and its `more`, each as an AnimatedPart of its own (their `more` empty);
// SetDoors writes such a list back (the first into its fields, the rest into `more`).
std::vector<AnimatedPart> DoorsOf(const AnimatedPart& part);
void SetDoors(AnimatedPart& part, const std::vector<AnimatedPart>& doors);
// Doors as a JSON list and back ({"sheet", "x", "y", "width", "height", "clip", "closeClip", "range", "opens", "locked",
// "key", "consumeKey"} each): `more`'s form, and a picture's .meta doors= (docs/assets/animations.md §4.4).
std::string DoorsJson(const std::vector<AnimatedPart>& doors);
std::vector<AnimatedPart> DoorsFromJson(const std::string& text);
// Its doorway is shut to walkers: a building one can go into whose animated door is closed and locked, or closed and
// opened by use (it opens for the player's shortcut, and for other characters as they come).
bool DoorwayShut(Registry& registry, Entity entity);

// Scene interiors linked both ways (docs/places/interiors.md §2b). Outside: `building`'s Interior becomes a scene
// interior leading to `scene`'s `marker`, and a marker `back` stands a step out of its doorway (where one comes out).
// False when it has no footprint.
bool LinkInteriorOutside(Registry& outside, Entity building, const std::string& scene, const std::string& marker, const std::string& back);
// Inside (the interior's own scene, its place over [x0, x1] x [y0, y1] world cells): a gateway out at its entrance (the
// open cell nearest the middle of its front, the highest y) to `exteriorScene`'s `back`, and the marker `marker` a
// couple of steps in. False when nothing there is open.
bool LinkInteriorInside(Registry& inside, int x0, int y0, int x1, int y1, const std::string& exteriorScene, const std::string& back,
                        const std::string& marker);

// The doorway of a footprint [x0, x1] x [y0, y1] on side `door` (s, e, n, w): the middle of that side.
inline std::pair<int, int> DoorwayOf(int x0, int y0, int x1, int y1, const std::string& door) {
    const int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    return {door == "e" ? x1 : door == "w" ? x0 : mx, door == "s" ? y1 : door == "n" ? y0 : my};
}

// The cells a building with an Interior blocks: its footprint's edge, less its doorway (`door`: s, e, n, w). Calls
// `blocked` for each. `sx`, `sy`: its world cell; left, right, back: its Obstacle's.
template <typename Fn>
void ForEachInteriorWall(int sx, int sy, int left, int right, int back, const std::string& door, Fn&& blocked) {
    const int x0 = sx - left, x1 = sx + right, y0 = sy - back, y1 = sy;
    const auto [dx, dy] = DoorwayOf(x0, y0, x1, y1, door);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const bool edge = x == x0 || x == x1 || y == y0 || y == y1;
            if (edge && !(x == dx && y == dy)) blocked(x, y);
        }
    }
}

// A scene interior's building: all its footprint blocks but its doorway.
template <typename Fn>
void ForEachSceneInteriorCell(int sx, int sy, int left, int right, int back, const std::string& door, Fn&& blocked) {
    const int x0 = sx - left, x1 = sx + right, y0 = sy - back, y1 = sy;
    const auto [dx, dy] = DoorwayOf(x0, y0, x1, y1, door);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!(x == dx && y == dy)) blocked(x, y);
        }
    }
}

} // namespace aige
