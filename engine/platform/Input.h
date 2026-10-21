#pragma once

#include <optional>
#include <string>

namespace aige {

// Every key, mouse button and gamepad button the engine knows (development_plan.md
// 12.3). Kept as our own enum (not GLFW or Win32 codes) so nothing outside the
// window backends needs to know which one is in use. Games don't read keys
// directly: they read input actions (gameplay/InputMap.h) bound to these.
//
// X(name): letters and digits are contiguous (A..Z, Num0..Num9), so code may
// step through them.
#define AIGE_KEYS(X)                                                                                               \
    X(Unknown)                                                                                                     \
    X(A) X(B) X(C) X(D) X(E) X(F) X(G) X(H) X(I) X(J) X(K) X(L) X(M) X(N) X(O) X(P) X(Q) X(R) X(S) X(T) X(U) X(V)  \
    X(W) X(X) X(Y) X(Z)                                                                                            \
    X(Num0) X(Num1) X(Num2) X(Num3) X(Num4) X(Num5) X(Num6) X(Num7) X(Num8) X(Num9)                                \
    X(F1) X(F2) X(F3) X(F4) X(F5) X(F6) X(F7) X(F8) X(F9) X(F10) X(F11) X(F12)                                     \
    X(Up) X(Down) X(Left) X(Right)                                                                                 \
    X(Space) X(Enter) X(Escape) X(Tab) X(Backspace)                                                                \
    X(LeftShift) X(RightShift) X(LeftCtrl) X(RightCtrl) X(LeftAlt) X(RightAlt)                                     \
    X(MouseLeft) X(MouseRight) X(MouseMiddle)                                                                      \
    X(PadA) X(PadB) X(PadX) X(PadY) X(PadLB) X(PadRB) X(PadBack) X(PadStart) X(PadLStick) X(PadRStick)             \
    X(PadUp) X(PadDown) X(PadLeft) X(PadRight) X(PadLT) X(PadRT)

enum class Key {
#define AIGE_KEY_ENUM(name) name,
    AIGE_KEYS(AIGE_KEY_ENUM)
#undef AIGE_KEY_ENUM
        Count
};

// A gamepad's sticks and triggers. Sticks are -1..1 with right and down
// positive; triggers 0..1.
enum class PadAxis {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

// "A", "Num1", "PadStart"... and back (input.json, the editor).
inline const char* KeyName(Key key) {
    static const char* const names[] = {
#define AIGE_KEY_NAME(name) #name,
        AIGE_KEYS(AIGE_KEY_NAME)
#undef AIGE_KEY_NAME
    };
    const int index = static_cast<int>(key);
    return index >= 0 && index < static_cast<int>(Key::Count) ? names[index] : "Unknown";
}

inline std::optional<Key> KeyFromName(const std::string& name) {
    for (int i = 1; i < static_cast<int>(Key::Count); ++i) {
        if (name == KeyName(static_cast<Key>(i))) return static_cast<Key>(i);
    }
    return std::nullopt;
}

inline const char* PadAxisName(PadAxis axis) {
    switch (axis) {
        case PadAxis::LeftX: return "LeftX";
        case PadAxis::LeftY: return "LeftY";
        case PadAxis::RightX: return "RightX";
        case PadAxis::RightY: return "RightY";
        case PadAxis::LeftTrigger: return "LeftTrigger";
        case PadAxis::RightTrigger: return "RightTrigger";
    }
    return "LeftX";
}

inline std::optional<PadAxis> PadAxisFromName(const std::string& name) {
    for (int i = 0; i <= static_cast<int>(PadAxis::RightTrigger); ++i) {
        if (name == PadAxisName(static_cast<PadAxis>(i))) return static_cast<PadAxis>(i);
    }
    return std::nullopt;
}

inline bool IsPadKey(Key key) { return key >= Key::PadA && key <= Key::PadRT; }
inline bool IsMouseKey(Key key) { return key >= Key::MouseLeft && key <= Key::MouseMiddle; }

} // namespace aige
