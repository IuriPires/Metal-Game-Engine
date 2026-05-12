// macOS window with a CAMetalLayer-backed content view. Lives in a single .mm
// translation unit per ADR-0001. Exposes a pure-C++ Window class; consumers
// receive the layer as an opaque void* via native_layer() and pass it to
// mge::rhi::Device::create_swapchain.

#include "mge/platform/window.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>

@interface MGEMetalView : NSView
@end

@implementation MGEMetalView
+ (Class)layerClass { return [CAMetalLayer class]; }
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
@end

@interface MGEWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) BOOL closed;
@property(nonatomic) BOOL resized;
@end

@implementation MGEWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    self.closed = YES;
    return YES;
}
- (void)windowWillClose:(NSNotification*)note {
    (void)note;
    self.closed = YES;
}
- (void)windowDidResize:(NSNotification*)note {
    (void)note;
    self.resized = YES;
}
- (void)windowDidChangeBackingProperties:(NSNotification*)note {
    (void)note;
    self.resized = YES;
}
@end

namespace mge::platform {

struct Window::Impl {
    NSWindow*            ns_window  = nil;
    MGEMetalView*        view       = nil;
    MGEWindowDelegate*   delegate   = nil;
    CAMetalLayer*        layer      = nil;
    std::uint32_t        width      = 0;
    std::uint32_t        height     = 0;
};

Window::Window(const WindowDesc& desc) : impl_(new Impl()) {
    @autoreleasepool {
        NSRect frame = NSMakeRect(0.0, 0.0,
                                  static_cast<CGFloat>(desc.width),
                                  static_cast<CGFloat>(desc.height));

        NSUInteger style = NSWindowStyleMaskTitled
                         | NSWindowStyleMaskClosable
                         | NSWindowStyleMaskMiniaturizable;
        if (desc.resizable) {
            style |= NSWindowStyleMaskResizable;
        }

        impl_->ns_window = [[NSWindow alloc] initWithContentRect:frame
                                                       styleMask:style
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];

        NSString* title = [[[NSString alloc] initWithBytes:desc.title.data()
                                                    length:desc.title.size()
                                                  encoding:NSUTF8StringEncoding]
                              autorelease];
        [impl_->ns_window setTitle:title];
        [impl_->ns_window center];

        impl_->view = [[MGEMetalView alloc] initWithFrame:frame];
        [impl_->view setWantsLayer:YES];

        impl_->layer = static_cast<CAMetalLayer*>([impl_->view layer]);
        [impl_->layer setPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB];
        [impl_->layer setFramebufferOnly:YES];
        if (desc.hi_dpi) {
            const CGFloat scale = [[NSScreen mainScreen] backingScaleFactor];
            [impl_->layer setContentsScale:scale];
            const CGFloat draw_w = static_cast<CGFloat>(desc.width) * scale;
            const CGFloat draw_h = static_cast<CGFloat>(desc.height) * scale;
            [impl_->layer setDrawableSize:CGSizeMake(draw_w, draw_h)];
        } else {
            [impl_->layer setDrawableSize:CGSizeMake(static_cast<CGFloat>(desc.width),
                                                     static_cast<CGFloat>(desc.height))];
        }

        [impl_->ns_window setContentView:impl_->view];
        [impl_->ns_window makeFirstResponder:impl_->view];

        impl_->delegate = [[MGEWindowDelegate alloc] init];
        [impl_->ns_window setDelegate:impl_->delegate];

        [impl_->ns_window makeKeyAndOrderFront:nil];

        impl_->width  = desc.width;
        impl_->height = desc.height;
    }
}

Window::~Window() {
    @autoreleasepool {
        if (impl_->ns_window) {
            [impl_->ns_window setDelegate:nil];
            [impl_->ns_window close];
            [impl_->ns_window release];
        }
        if (impl_->delegate) {
            [impl_->delegate release];
        }
        if (impl_->view) {
            [impl_->view release];
        }
    }
    delete impl_;
}

bool Window::should_close() const noexcept {
    return impl_->delegate.closed == YES;
}

void Window::request_close() noexcept {
    @autoreleasepool {
        [impl_->ns_window performClose:nil];
    }
}

std::uint32_t Window::width() const noexcept {
    @autoreleasepool {
        NSRect r = [[impl_->ns_window contentView] frame];
        return static_cast<std::uint32_t>(r.size.width);
    }
}

std::uint32_t Window::height() const noexcept {
    @autoreleasepool {
        NSRect r = [[impl_->ns_window contentView] frame];
        return static_cast<std::uint32_t>(r.size.height);
    }
}

std::uint32_t Window::drawable_width() const noexcept {
    @autoreleasepool {
        return static_cast<std::uint32_t>([impl_->layer drawableSize].width);
    }
}

std::uint32_t Window::drawable_height() const noexcept {
    @autoreleasepool {
        return static_cast<std::uint32_t>([impl_->layer drawableSize].height);
    }
}

bool Window::consume_resize_event() noexcept {
    @autoreleasepool {
        const BOOL was = impl_->delegate.resized;
        impl_->delegate.resized = NO;
        if (was) {
            // Sync the CAMetalLayer drawable size to the view's backing size.
            const CGSize  vsize  = [impl_->view convertSizeToBacking:[impl_->view bounds].size];
            const CGFloat scale  = [[impl_->ns_window screen] backingScaleFactor];
            const CGFloat draw_w = vsize.width  > 0.0 ? vsize.width  : [impl_->view bounds].size.width  * scale;
            const CGFloat draw_h = vsize.height > 0.0 ? vsize.height : [impl_->view bounds].size.height * scale;
            [impl_->layer setDrawableSize:CGSizeMake(draw_w, draw_h)];
        }
        return was == YES;
    }
}

void* Window::native_layer() noexcept {
    // CAMetalLayer* is layout-compatible with metal-cpp's CA::MetalLayer*.
    // Consumers cast to whichever they need.
    return static_cast<void*>(impl_->layer);
}

}  // namespace mge::platform
