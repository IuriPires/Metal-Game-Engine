#pragma once

#include <string>

namespace mge::platform {

// Per-process application lifecycle. On macOS this owns the singleton
// NSApplication and sets the activation policy. Phase 1 is intentionally
// minimal - no menu bar, no Apple-event hookups, no full event-loop control.
//
// Construction must happen on the main thread.
class App {
public:
    static App& get();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    void set_name(std::string_view name);

    // Drain the application's event queue once. Non-blocking. Call once per
    // frame from the main thread before rendering.
    void poll_events();

    // True once a window has signaled close. Convenience for game loop.
    [[nodiscard]] bool should_quit() const noexcept;
    void               request_quit() noexcept;

private:
    App();
    ~App();

    struct Impl;
    Impl* impl_;
};

}  // namespace mge::platform
