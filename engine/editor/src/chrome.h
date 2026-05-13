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
void draw_chrome(const EngineState& state, Selection& selection,
                  bool* viewport_hovered_out = nullptr);

}  // namespace mge::editor
