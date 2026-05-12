#pragma once

#include "mge/editor/editor.h"

namespace mge::editor {

// Drives the editor chrome: menubar, toolbar, three-column workarea,
// bottom dock, status bar. M18 wires the chrome with placeholder panels —
// concrete outliner / inspector / profiler / framegraph content lands in
// M19+ as the design specifies.
void draw_chrome(const EngineState& state);

}  // namespace mge::editor
