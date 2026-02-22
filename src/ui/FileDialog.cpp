#include "ui/FileDialog.h"
#include <ImGuiFileDialog.h>

void FileDialog::open() {
    IGFD::FileDialogConfig config;
    config.path = ".";
    ImGuiFileDialog::Instance()->OpenDialog(
        "OpenFileDlg", "Open Media File",
        ".mp4,.mkv,.avi,.mov,.webm,.flv,.wmv,.ts,.m4v,.png,.jpg,.jpeg,.bmp,.tga,.*",
        config
    );
}

void FileDialog::render() {
    if (ImGuiFileDialog::Instance()->Display("OpenFileDlg")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string path = ImGuiFileDialog::Instance()->GetFilePathName();
            if (m_callback) m_callback(path);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}
