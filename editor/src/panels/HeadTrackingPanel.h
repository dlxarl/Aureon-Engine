#pragma once
#include "EditorContext.h"

// Head tracking for the Game view: which sensor to use, what it reports, and how the view
// follows the head. The diagnostics are live whether or not the game is playing.
class HeadTrackingPanel {
public:
    void draw(EditorContext& ctx, bool* open);

private:
    void drawProvider(EditorContext& ctx);
    void drawDiagnostics(EditorContext& ctx);
    void drawSettings(EditorContext& ctx);
};
