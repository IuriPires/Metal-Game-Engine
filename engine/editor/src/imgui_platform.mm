// Editor platform shim. Bridges ImGui's upstream Metal + OSX backends into
// the engine's RHI/window. Lives at the platform edge per ADR-0001's
// "Obj-C++ exception for AppKit boundaries" clause; ADR-0014 records the
// explicit decision to do this for the editor rather than reimplement the
// renderer through our RHI.

#include "imgui_platform.h"

#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>

namespace mge::editor::platform {

bool init_metal(void* native_mtl_device) {
    auto* dev = static_cast<id<MTLDevice>>(native_mtl_device);
    return ImGui_ImplMetal_Init(dev);
}

void shutdown_metal() {
    ImGui_ImplMetal_Shutdown();
}

bool init_osx(void* native_nswindow) {
    auto* win = static_cast<NSWindow*>(native_nswindow);
    if (win == nil) return false;
    return ImGui_ImplOSX_Init(win.contentView);
}

void shutdown_osx() {
    ImGui_ImplOSX_Shutdown();
}

void new_frame(void*         native_nswindow,
                std::uint32_t drawable_width,
                std::uint32_t drawable_height,
                void*         color_texture,
                void*         /*depth_texture*/) {
    auto* win = static_cast<NSWindow*>(native_nswindow);
    ImGui_ImplOSX_NewFrame(win ? win.contentView : nil);

    MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
    rpd.colorAttachments[0].texture     = static_cast<id<MTLTexture>>(color_texture);
    rpd.colorAttachments[0].loadAction  = MTLLoadActionLoad;
    rpd.colorAttachments[0].storeAction = MTLStoreActionStore;

    ImGui_ImplMetal_NewFrame(rpd);

    ImGuiIO& io       = ImGui::GetIO();
    io.DisplaySize.x  = static_cast<float>(drawable_width);
    io.DisplaySize.y  = static_cast<float>(drawable_height);
    io.DisplayFramebufferScale =
        ImVec2(1.0f, 1.0f);  // we already use physical pixels everywhere
}

void render_into_encoder(void* command_buffer_native, void* encoder_native) {
    auto* cmd = static_cast<id<MTLCommandBuffer>>(command_buffer_native);
    auto* enc = static_cast<id<MTLRenderCommandEncoder>>(encoder_native);
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cmd, enc);
}

}  // namespace mge::editor::platform
