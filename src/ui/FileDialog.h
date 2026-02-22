#pragma once

#include <string>
#include <functional>

class FileDialog {
public:
    void open();
    void render();

    using Callback = std::function<void(const std::string&)>;
    void setCallback(Callback cb) { m_callback = std::move(cb); }

private:
    Callback m_callback;
};
