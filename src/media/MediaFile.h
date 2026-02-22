#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <string>

class MediaFile {
public:
    ~MediaFile();

    bool open(const std::string& path);
    void close();

    AVFormatContext* getFormatContext() const { return m_formatCtx; }
    int getVideoStreamIndex() const { return m_videoStreamIdx; }
    int getAudioStreamIndex() const { return m_audioStreamIdx; }
    AVCodecParameters* getVideoCodecPar() const;
    AVCodecParameters* getAudioCodecPar() const;
    AVStream* getVideoStream() const;
    AVStream* getAudioStream() const;
    double getDuration() const;
    const std::string& getPath() const { return m_path; }

    bool isOpen() const { return m_formatCtx != nullptr; }

private:
    AVFormatContext* m_formatCtx = nullptr;
    int m_videoStreamIdx = -1;
    int m_audioStreamIdx = -1;
    std::string m_path;
};
