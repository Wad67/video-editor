#include "media/AudioDecoder.h"
#include "media/PacketQueue.h"
#include "media/AudioFrameQueue.h"
#include <cstdio>

AudioDecoder::~AudioDecoder() {
    stop();
    if (m_swrCtx) swr_free(&m_swrCtx);
    if (m_codecCtx) avcodec_free_context(&m_codecCtx);
}

bool AudioDecoder::init(AVCodecParameters* codecPar, AVRational timeBase, int outputSampleRate) {
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        fprintf(stderr, "Unsupported audio codec\n");
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, codecPar);

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        fprintf(stderr, "Could not open audio codec\n");
        return false;
    }

    m_timeBase = timeBase;
    // Use specified output sample rate, or source rate if not specified
    int outRate = (outputSampleRate > 0) ? outputSampleRate : m_codecCtx->sample_rate;
    m_sampleRate = outRate;
    m_channels = 2; // Always stereo output

    // Init resampler: convert to interleaved float stereo at target rate
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    int ret = swr_alloc_set_opts2(&m_swrCtx,
        &outLayout, AV_SAMPLE_FMT_FLT, outRate,
        &m_codecCtx->ch_layout, m_codecCtx->sample_fmt, m_codecCtx->sample_rate,
        0, nullptr);
    if (ret < 0 || swr_init(m_swrCtx) < 0) {
        fprintf(stderr, "Could not init audio resampler\n");
        return false;
    }

    return true;
}

void AudioDecoder::start(PacketQueue& packetQueue, AudioFrameQueue& frameQueue) {
    m_running.store(true);
    m_thread = std::thread(&AudioDecoder::decodeLoop, this,
                           std::ref(packetQueue), std::ref(frameQueue));
}

void AudioDecoder::stop() {
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
}

void AudioDecoder::decodeLoop(PacketQueue& packetQueue, AudioFrameQueue& frameQueue) {
    AVFrame* decoded = av_frame_alloc();
    AVFrame* resampled = av_frame_alloc();

    int serial = packetQueue.getSerial();

    while (m_running.load()) {
        AVPacket* pkt = packetQueue.pop(50);
        if (!pkt) continue;

        int newSerial = packetQueue.getSerial();
        if (newSerial != serial) {
            avcodec_flush_buffers(m_codecCtx);
            serial = newSerial;
        }

        int ret = avcodec_send_packet(m_codecCtx, pkt);
        av_packet_free(&pkt);
        if (ret < 0) continue;

        while (ret >= 0 && m_running.load()) {
            ret = avcodec_receive_frame(m_codecCtx, decoded);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            // Resample to interleaved float stereo at output rate
            resampled->format = AV_SAMPLE_FMT_FLT;
            resampled->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
            resampled->sample_rate = m_sampleRate;
            // Estimate output samples (may differ if resampling rate changes)
            resampled->nb_samples = av_rescale_rnd(
                decoded->nb_samples, m_sampleRate, m_codecCtx->sample_rate, AV_ROUND_UP);

            ret = swr_convert_frame(m_swrCtx, resampled, decoded);
            if (ret < 0) {
                av_frame_unref(decoded);
                av_frame_unref(resampled);
                continue;
            }

            resampled->pts = decoded->pts;
            if (resampled->pts == AV_NOPTS_VALUE)
                resampled->pts = decoded->best_effort_timestamp;

            frameQueue.push(resampled, serial);

            av_frame_unref(decoded);
        }
    }

    av_frame_free(&decoded);
    av_frame_free(&resampled);
}
