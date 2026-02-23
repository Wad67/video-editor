#include "export/AudioEncoder.h"
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

AudioEncoder::~AudioEncoder() {
    shutdown();
}

bool AudioEncoder::init(const ExportSettings& settings, int muxerFlags) {
    m_channels = settings.audioChannels;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        fprintf(stderr, "AudioEncoder: AAC encoder not found\n");
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) return false;

    m_codecCtx->sample_rate = settings.audioSampleRate;
    m_codecCtx->bit_rate = settings.audioBitrate;
    AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&m_codecCtx->ch_layout, &layout);
    m_codecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_codecCtx->time_base = AVRational{1, settings.audioSampleRate};

    if (muxerFlags & AVFMT_GLOBALHEADER) {
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "AudioEncoder: cannot open codec: %s\n", errbuf);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    // Setup resampler: interleaved float -> planar float (same rate)
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    ret = swr_alloc_set_opts2(&m_swrCtx,
        &stereo, AV_SAMPLE_FMT_FLTP, settings.audioSampleRate,
        &stereo, AV_SAMPLE_FMT_FLT, settings.audioSampleRate,
        0, nullptr);
    if (ret < 0 || !m_swrCtx) {
        fprintf(stderr, "AudioEncoder: cannot create resampler\n");
        shutdown();
        return false;
    }
    swr_init(m_swrCtx);

    // Allocate output frame
    m_frame = av_frame_alloc();
    m_frame->format = AV_SAMPLE_FMT_FLTP;
    m_frame->sample_rate = settings.audioSampleRate;
    av_channel_layout_copy(&m_frame->ch_layout, &layout);
    m_frame->nb_samples = m_codecCtx->frame_size;
    ret = av_frame_get_buffer(m_frame, 0);
    if (ret < 0) {
        fprintf(stderr, "AudioEncoder: cannot allocate frame buffer\n");
        shutdown();
        return false;
    }

    // Pre-allocate input buffer for accumulation
    m_inputBuffer.resize(m_codecCtx->frame_size * m_channels);
    m_samplesBuffered = 0;
    m_nextPts = 0;

    fprintf(stderr, "[EXPORT] Audio encoder: AAC %d Hz, %d kbps, frame_size=%d\n",
            settings.audioSampleRate, settings.audioBitrate / 1000,
            m_codecCtx->frame_size);
    return true;
}

void AudioEncoder::shutdown() {
    if (m_frame) av_frame_free(&m_frame);
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
    if (m_codecCtx) avcodec_free_context(&m_codecCtx);
    m_inputBuffer.clear();
    m_samplesBuffered = 0;
    m_nextPts = 0;
}

int AudioEncoder::getFrameSize() const {
    return m_codecCtx ? m_codecCtx->frame_size : 1024;
}

bool AudioEncoder::encode(const float* samples, int numFrames, PacketCallback cb) {
    if (!m_codecCtx || !m_swrCtx) return false;

    int samplesRemaining = numFrames;
    const float* src = samples;

    while (samplesRemaining > 0) {
        int frameSize = m_codecCtx->frame_size;
        int spaceInBuffer = frameSize - m_samplesBuffered;
        int toCopy = std::min(samplesRemaining, spaceInBuffer);

        // Accumulate into input buffer
        memcpy(m_inputBuffer.data() + m_samplesBuffered * m_channels,
               src, toCopy * m_channels * sizeof(float));
        m_samplesBuffered += toCopy;
        src += toCopy * m_channels;
        samplesRemaining -= toCopy;

        // If we have a full frame, encode it
        if (m_samplesBuffered >= frameSize) {
            if (!encodeBuffered(cb)) return false;
        }
    }

    return true;
}

bool AudioEncoder::encodeBuffered(PacketCallback& cb) {
    av_frame_make_writable(m_frame);

    // Convert interleaved float -> planar float
    const uint8_t* inData[1] = {
        reinterpret_cast<const uint8_t*>(m_inputBuffer.data())
    };
    int ret = swr_convert(m_swrCtx,
        m_frame->data, m_frame->nb_samples,
        inData, m_samplesBuffered);
    if (ret < 0) {
        fprintf(stderr, "AudioEncoder: swr_convert failed\n");
        return false;
    }

    m_frame->pts = m_nextPts;
    m_nextPts += m_samplesBuffered;
    m_samplesBuffered = 0;

    ret = avcodec_send_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "AudioEncoder: send_frame failed: %s\n", errbuf);
        return false;
    }

    return drainPackets(cb);
}

bool AudioEncoder::flush(PacketCallback cb) {
    if (!m_codecCtx) return false;

    // Encode any remaining buffered samples (partial frame)
    if (m_samplesBuffered > 0) {
        av_frame_make_writable(m_frame);
        m_frame->nb_samples = m_samplesBuffered;

        const uint8_t* inData[1] = {
            reinterpret_cast<const uint8_t*>(m_inputBuffer.data())
        };
        swr_convert(m_swrCtx,
            m_frame->data, m_samplesBuffered,
            inData, m_samplesBuffered);

        m_frame->pts = m_nextPts;
        avcodec_send_frame(m_codecCtx, m_frame);
        drainPackets(cb);

        m_frame->nb_samples = m_codecCtx->frame_size;
        m_samplesBuffered = 0;
    }

    // Flush encoder
    avcodec_send_frame(m_codecCtx, nullptr);
    return drainPackets(cb);
}

bool AudioEncoder::drainPackets(PacketCallback& cb) {
    AVPacket* pkt = av_packet_alloc();
    while (true) {
        int ret = avcodec_receive_packet(m_codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            av_packet_free(&pkt);
            return false;
        }
        cb(pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return true;
}
