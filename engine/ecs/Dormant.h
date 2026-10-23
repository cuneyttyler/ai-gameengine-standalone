#pragma once

namespace aige {

// Streaming big scenes (development_plan.md 24): an entity far from the player, outside the
// active chunks. Scripts, character AI, routines and spawners leave it alone until the
// player comes near (gameplay/Streaming); drawing culls it as anything off screen.
struct Dormant {};

} // namespace aige
