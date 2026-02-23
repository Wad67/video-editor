#pragma once

#include "export/ExportSettings.h"
#include "export/ExportSession.h"

class ExportDialog {
public:
    // Render the settings dialog. Returns true if user clicked Export.
    bool render(ExportSettings& settings, bool& visible);

    // Render progress overlay while exporting.
    void renderProgress(ExportSession& session);

    // Set source dimensions for "Source" resolution option
    void setSourceInfo(int w, int h, double fps) {
        m_sourceWidth = w; m_sourceHeight = h; m_sourceFps = fps;
    }

private:
    char m_outputPath[512] = "output.mp4";
    int m_codecIndex = 0;
    int m_resIndex = 0;  // 0=source, 1=1080p, 2=720p, 3=480p
    int m_fpsIndex = 0;  // 0=source, 1=60, 2=30, 3=24

    // Source dimensions (set from settings before render)
    int m_sourceWidth = 0;
    int m_sourceHeight = 0;
    double m_sourceFps = 0.0;
};
