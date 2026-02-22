#include "media/MediaFile.h"
#include <cstdio>

MediaFile::~MediaFile() {
    close();
}

bool MediaFile::open(const std::string& path) {
    close();
    m_path = path;

    if (avformat_open_input(&m_formatCtx, path.c_str(), nullptr, nullptr) < 0) {
        fprintf(stderr, "Could not open file: %s\n", path.c_str());
        return false;
    }

    if (avformat_find_stream_info(m_formatCtx, nullptr) < 0) {
        fprintf(stderr, "Could not find stream info\n");
        close();
        return false;
    }

    for (unsigned i = 0; i < m_formatCtx->nb_streams; i++) {
        auto codecType = m_formatCtx->streams[i]->codecpar->codec_type;
        if (codecType == AVMEDIA_TYPE_VIDEO && m_videoStreamIdx < 0) {
            m_videoStreamIdx = i;
        } else if (codecType == AVMEDIA_TYPE_AUDIO && m_audioStreamIdx < 0) {
            m_audioStreamIdx = i;
        }
    }

    if (m_videoStreamIdx < 0) {
        fprintf(stderr, "No video stream found\n");
        close();
        return false;
    }

    return true;
}

void MediaFile::close() {
    if (m_formatCtx) {
        avformat_close_input(&m_formatCtx);
    }
    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;
    m_path.clear();
}

AVCodecParameters* MediaFile::getVideoCodecPar() const {
    if (!m_formatCtx || m_videoStreamIdx < 0) return nullptr;
    return m_formatCtx->streams[m_videoStreamIdx]->codecpar;
}

AVCodecParameters* MediaFile::getAudioCodecPar() const {
    if (!m_formatCtx || m_audioStreamIdx < 0) return nullptr;
    return m_formatCtx->streams[m_audioStreamIdx]->codecpar;
}

AVStream* MediaFile::getVideoStream() const {
    if (!m_formatCtx || m_videoStreamIdx < 0) return nullptr;
    return m_formatCtx->streams[m_videoStreamIdx];
}

AVStream* MediaFile::getAudioStream() const {
    if (!m_formatCtx || m_audioStreamIdx < 0) return nullptr;
    return m_formatCtx->streams[m_audioStreamIdx];
}

double MediaFile::getDuration() const {
    if (!m_formatCtx) return 0.0;
    if (m_formatCtx->duration != AV_NOPTS_VALUE)
        return static_cast<double>(m_formatCtx->duration) / AV_TIME_BASE;
    return 0.0;
}
