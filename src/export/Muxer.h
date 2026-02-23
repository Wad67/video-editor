#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <string>

class Muxer {
public:
    ~Muxer();

    bool open(const std::string& path, const std::string& formatName = "mp4");

    int addVideoStream(const AVCodecContext* codecCtx);
    int addAudioStream(const AVCodecContext* codecCtx);

    bool writeHeader();
    bool writePacket(AVPacket* pkt);
    bool writeTrailer();
    void close();

    AVFormatContext* getFormatContext() { return m_fmtCtx; }
    AVStream* getVideoStream() { return m_videoStream; }
    AVStream* getAudioStream() { return m_audioStream; }
    int getVideoStreamIndex() const { return m_videoStreamIdx; }
    int getAudioStreamIndex() const { return m_audioStreamIdx; }

private:
    AVFormatContext* m_fmtCtx = nullptr;
    AVStream* m_videoStream = nullptr;
    AVStream* m_audioStream = nullptr;
    int m_videoStreamIdx = -1;
    int m_audioStreamIdx = -1;
    bool m_headerWritten = false;
};
