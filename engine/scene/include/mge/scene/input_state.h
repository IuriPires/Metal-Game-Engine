#pragma once

// M26a — Frame-local input snapshot consumed by camera controllers and any
// other interactive system. The struct itself is platform-agnostic: a layer
// at the platform / editor edge populates it each frame (today from ImGuiIO,
// tomorrow from NSEvent capture for the editor-off path) and hands it to the
// controller. Pure data, no behaviour — keeps controllers unit-testable.

#include <array>
#include <cstdint>

namespace mge::scene {

// A small, hand-picked set of keys — enough to drive fly + orbit cameras and
// the standard editor hotkeys. Indices double as `keys_down` bit positions.
// Add more as needed; the enum is closed-set on purpose so the InputState
// stays a fixed-size struct (cheap to copy in unit tests).
enum class Key : std::uint8_t {
    Unknown = 0,
    W, A, S, D, Q, E,
    F,   // frame selection (future)
    Tab, // controller toggle
    Esc,
    Space,
    LeftBracket, RightBracket,
    Digit1, Digit3, Digit7,  // ortho views (M26b)
    COUNT,
};

inline constexpr std::size_t k_key_count = static_cast<std::size_t>(Key::COUNT);

// Mouse buttons. Index = enumerator value, so `buttons[Button::Left]` works
// after a cast — keep parity with ImGui's 0=L, 1=R, 2=M layout (we swap order
// at the populate boundary, not here).
enum class Button : std::uint8_t {
    Left   = 0,
    Right  = 1,
    Middle = 2,
    COUNT,
};

inline constexpr std::size_t k_button_count = static_cast<std::size_t>(Button::COUNT);

// Bitfield of modifier keys held during the frame.
struct Modifiers {
    bool shift = false;
    bool ctrl  = false;
    bool alt   = false;
    bool super = false;  // cmd on macOS

    [[nodiscard]] constexpr bool any() const noexcept {
        return shift || ctrl || alt || super;
    }
};

// Per-frame input snapshot. Both the position-style and delta-style fields
// are filled — controllers pick whichever fits their motion model.
//
// Coordinate space: `mouse_pos` is in *logical* points (same space ImGui
// uses for hit-testing), origin top-left. `mouse_delta` is the frame's
// movement in the same units. `scroll` is wheel ticks (typically ±1 per
// notch on a discrete wheel, sub-1 on touchpad inertia).
//
// `viewport_hovered` and `keyboard_focused` are routing hints from the host:
// when the editor is on, set them to false if the cursor is over an ImGui
// panel / a text field has focus, so the controller knows to ignore the
// otherwise-valid mouse and key state. Keeps the demo's responsibility
// "build a snapshot" rather than "decide who gets the event".
struct InputState {
    // Mouse.
    float mouse_pos_x  = 0.0f;
    float mouse_pos_y  = 0.0f;
    float mouse_delta_x = 0.0f;
    float mouse_delta_y = 0.0f;
    float scroll_x     = 0.0f;
    float scroll_y     = 0.0f;

    std::array<bool, k_button_count> button_down{};
    std::array<bool, k_button_count> button_pressed{};   // edge (LMB↑→↓)
    std::array<bool, k_button_count> button_released{};  // edge (LMB↓→↑)

    // Keyboard.
    std::array<bool, k_key_count> key_down{};
    std::array<bool, k_key_count> key_pressed{};         // edge

    Modifiers mods{};

    // Routing hints (see struct comment).
    bool viewport_hovered = true;
    bool keyboard_focused = false;

    [[nodiscard]] bool is_down(Button b) const noexcept {
        return button_down[static_cast<std::size_t>(b)];
    }
    [[nodiscard]] bool is_pressed(Button b) const noexcept {
        return button_pressed[static_cast<std::size_t>(b)];
    }
    [[nodiscard]] bool is_released(Button b) const noexcept {
        return button_released[static_cast<std::size_t>(b)];
    }
    [[nodiscard]] bool is_down(Key k) const noexcept {
        return key_down[static_cast<std::size_t>(k)];
    }
    [[nodiscard]] bool is_pressed(Key k) const noexcept {
        return key_pressed[static_cast<std::size_t>(k)];
    }
};

}  // namespace mge::scene
