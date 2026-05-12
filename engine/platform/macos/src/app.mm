// Minimal NSApplication wrapper. Lives in a single .mm translation unit per
// ADR-0001 (Metal-cpp pure C++ everywhere except this AppKit edge).

#include "mge/platform/app.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace mge::platform {

struct App::Impl {
    bool   should_quit = false;
    NSAutoreleasePool* pool = nullptr;
};

namespace {

App* g_instance = nullptr;

void ensure_nsapp() {
    @autoreleasepool {
        if (NSApp == nil) {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            [NSApp finishLaunching];
            [NSApp activateIgnoringOtherApps:YES];
        }
    }
}

}  // namespace

App& App::get() {
    if (g_instance == nullptr) {
        g_instance = new App();
    }
    return *g_instance;
}

App::App() : impl_(new Impl()) {
    ensure_nsapp();
}

App::~App() {
    delete impl_;
}

void App::set_name(std::string_view name) {
    @autoreleasepool {
        NSString* ns = [[[NSString alloc] initWithBytes:name.data()
                                                 length:name.size()
                                               encoding:NSUTF8StringEncoding] autorelease];
        // Setting the process name in the menu bar is non-trivial without an
        // Info.plist - this only updates the activation menu label.
        [[NSProcessInfo processInfo] setProcessName:ns];
    }
}

void App::poll_events() {
    @autoreleasepool {
        for (;;) {
            NSEvent* evt = [NSApp nextEventMatchingMask:NSEventMaskAny
                                              untilDate:[NSDate distantPast]
                                                 inMode:NSDefaultRunLoopMode
                                                dequeue:YES];
            if (evt == nil) {
                break;
            }
            [NSApp sendEvent:evt];
        }
    }
}

bool App::should_quit() const noexcept {
    return impl_->should_quit;
}

void App::request_quit() noexcept {
    impl_->should_quit = true;
}

}  // namespace mge::platform
