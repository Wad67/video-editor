#include "export/Muxer.h"
#include <cstdio>

Muxer::~Muxer() {
    close();
}

bool Muxer::open(const std::string& path, const std::string& formatName) {
    int ret = avformat_alloc_output_context2(
        &m_fmtCtx, nullptr, formatName.c_str(), path.c_str());
    if (ret < 0 || !m_fmtCtx) {
        fprintf(stderr, "Muxer: cannot allocate output context for %s\n", path.c_str());
        return false;
    }

    if (!(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_fmtCtx->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            fprintf(stderr, "Muxer: cannot open output file %s: %s\n",
                    path.c_str(), errbuf);
            avformat_free_context(m_fmtCtx);
            m_fmtCtx = nullptr;
            return false;
        }
    }

    return true;
}

int Muxer::addVideoStream(const AVCodecContext* codecCtx) {
    if (!m_fmtCtx) return -1;

    m_videoStream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_videoStream) {
        fprintf(stderr, "Muxer: cannot create video stream\n");
        return -1;
    }

    avcodec_parameters_from_context(m_videoStream->codecpar, codecCtx);
    m_videoStream->time_base = codecCtx->time_base;
    m_videoStreamIdx = m_videoStream->index;
    return m_videoStreamIdx;
}

int Muxer::addAudioStream(const AVCodecContext* codecCtx) {
    if (!m_fmtCtx) return -1;

    m_audioStream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_audioStream) {
        fprintf(stderr, "Muxer: cannot create audio stream\n");
        return -1;
    }

    avcodec_parameters_from_context(m_audioStream->codecpar, codecCtx);
    m_audioStream->time_base = codecCtx->time_base;
    m_audioStreamIdx = m_audioStream->index;
    return m_audioStreamIdx;
}

bool Muxer::writeHeader() {
    if (!m_fmtCtx) return false;

    int ret = avformat_write_header(m_fmtCtx, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Muxer: cannot write header: %s\n", errbuf);
        return false;
    }

    m_headerWritten = true;
    return true;
}

bool Muxer::writePacket(AVPacket* pkt) {
    if (!m_fmtCtx || !m_headerWritten) return false;

    int ret = av_interleaved_write_frame(m_fmtCtx, pkt);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Muxer: write packet failed: %s\n", errbuf);
        return false;
    }
    return true;
}

bool Muxer::writeTrailer() {
    if (!m_fmtCtx || !m_headerWritten) return false;

    int ret = av_write_trailer(m_fmtCtx);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "Muxer: write trailer failed: %s\n", errbuf);
        return false;
    }
    return true;
}

void Muxer::close() {
    if (m_fmtCtx) {
        if (m_headerWritten) {
            // writeTrailer should already have been called, but be safe
        }
        if (m_fmtCtx->pb && !(m_fmtCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_fmtCtx->pb);
        }
        avformat_free_context(m_fmtCtx);
        m_fmtCtx = nullptr;
    }
    m_videoStream = nullptr;
    m_audioStream = nullptr;
    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;
    m_headerWritten = false;
}
