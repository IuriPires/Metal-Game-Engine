#pragma once

#include "mge/editor/editor.h"

namespace mge::editor {

// Drives the editor chrome: menubar, toolbar, three-column workarea,
// bottom dock, status bar. M19a brought real fonts; M19b wires Outliner +
// Inspector to actual scene state. Concrete profiler / framegraph / render
// settings / shader-reload tabs land in M20.
// `viewport_hovered_out` (if non-null) receives whether the mouse cursor is
// currently over the central viewport child window — used by the demo to
// route camera input only when the user isn't over any panel.
//
// `gizmo_op` selects the ImGuizmo::OPERATION mode (cast via int to keep the
// chrome header free of the ImGuizmo include). `gizmo_active_out` receives
// `ImGuizmo::IsUsing()` after the manipulator runs; nullable.
//
// M28a — gizmo Manipulate() is called from INSIDE the ##center child's
// BeginChild scope so ImGuizmo uses that window's draw list + screen rect
// (canonical pattern). Hosting it after draw_chrome returned would let the
// transparent center child's mouse capture eat the click before ImGuizmo
// got a chance to claim it.
void draw_chrome(const EngineState& state, Selection& selection,
                  bool* viewport_hovered_out = nullptr,
                  int   gizmo_op             = 0,
                  bool* gizmo_active_out     = nullptr);

}  // namespace mge::editor
