# Video Editor

An experimental non-linear video editor built with Vulkan and SDL3.

Born out of frustration with kdenlive, shotcut, and blender's video editing — this project aims to be a fast, GPU-accelerated editor that doesn't get in your way.

**Status:** Early development / experimental.

## Features

- Vulkan-based rendering with triple-buffered video display
- Multi-threaded media pipeline (demux, video decode, audio decode)
- Audio-driven sync — video follows the audio master clock
- Timeline editing with multi-track support and clip manipulation
- Dear ImGui (docking branch) interface with drag-and-drop panels
- FFmpeg-powered format support

## Building

### System Requirements

- **C++20 compiler** (GCC 13+ or Clang 17+)
- **CMake 3.24+**
- **Vulkan SDK 1.2+** (headers, loader, and validation layers)
- **FFmpeg 6.x** (development libraries)
- **pkg-config**

The following dependencies are fetched automatically via CMake FetchContent:
- SDL3
- Dear ImGui (docking branch)
- ImGuiFileDialog
- Vulkan Memory Allocator (VMA)

### Package Installation

**Debian / Ubuntu:**
```bash
sudo apt install build-essential cmake pkg-config \
    libvulkan-dev vulkan-validationlayers \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
```

**Fedora:**
```bash
sudo dnf install gcc-c++ cmake pkg-config \
    vulkan-devel vulkan-validation-layers \
    ffmpeg-free-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake pkgconf \
    vulkan-devel vulkan-validation-layers \
    ffmpeg
```

### Compile

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

For a debug build with symbols:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### Run

```bash
./build/video-editor              # launch empty
./build/video-editor video.mp4    # open a file directly
```

## Project Structure

```
src/
├── main.cpp              Entry point
├── app/                   Application lifecycle and main loop
├── media/                 Demuxing, decoding, audio output, playback control
├── timeline/              Timeline, tracks, clips, and timeline playback
├── ui/                    ImGui panels (player, timeline, clip properties)
└── vulkan/                Vulkan context, swapchain, and texture management
cmake/
├── FetchDependencies.cmake
└── FindFFmpeg.cmake
```

## License

[WTFPL](http://www.wtfpl.net/) — Do What The Fuck You Want To Public License.
