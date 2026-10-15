#pragma once

namespace aige {

// Minimal key set for Phase 0 bootstrap content; grows as real gameplay
// needs it. Kept as our own enum (not GLFW key codes) so nothing outside
// Window.cpp needs to know the windowing backend exists.
enum class Key {
    W,
    A,
    S,
    D,
    Up,
    Down,
    Left,
    Right,
    Escape,
    F5,
    Space,
};

} // namespace aige
