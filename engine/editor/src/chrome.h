#pragma once

#include "mge/editor/editor.h"

namespace mge::editor {

// Drives the editor chrome: menubar, toolbar, three-column workarea,
// bottom dock, status bar. M19a brought real fonts; M19b wires Outliner +
// Inspector to actual scene state. Concrete profiler / framegraph / render
// settings / shader-reload tabs land in M20.
void draw_chrome(const EngineState& state, Selection& selection);

}  // namespace mge::editor
