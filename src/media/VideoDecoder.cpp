#include "media/VideoDecoder.h"
#include "media/PacketQueue.h"
#include "media/FrameQueue.h"
#include "media/DebugStats.h"
#include <cstdio>

extern "C" {
#include <libavutil/imgutils.h>
}

VideoDecoder::~VideoDecoder() {
    stop();
    if (m_swsCtx) sws_freeContext(m_swsCtx);
    if (m_codecCtx) avcodec_free_context(&m_codecCtx);
}

bool VideoDecoder::init(AVCodecParameters* codecPar, AVRational timeBase, AVRational frameRate) {
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        fprintf(stderr, "Unsupported video codec\n");
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, codecPar);
    m_codecCtx->thread_count = 0;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        fprintf(stderr, "Could not open video codec\n");
        return false;
    }

    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;
    m_timeBase = timeBase;

    // Use the stream's avg_frame_rate (from container), not codec context framerate
    if (frameRate.num > 0 && frameRate.den > 0) {
        m_frameRate = av_q2d(frameRate);
    }
    fprintf(stderr, "Video: %dx%d, %.2f fps, time_base=%d/%d\n",
            m_width, m_height, m_frameRate, timeBase.num, timeBase.den);

    m_swsCtx = sws_getContext(
        m_width, m_height, m_codecCtx->pix_fmt,
        m_width, m_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!m_swsCtx) {
        fprintf(stderr, "Could not create sws context\n");
        return false;
    }

    return true;
}

void VideoDecoder::start(PacketQueue& packetQueue, FrameQueue& frameQueue) {
    m_running.store(true);
    m_thread = std::thread(&VideoDecoder::decodeLoop, this,
                           std::ref(packetQueue), std::ref(frameQueue));
}

void VideoDecoder::stop() {
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
}

void VideoDecoder::decodeLoop(PacketQueue& packetQueue, FrameQueue& frameQueue) {
    AVFrame* decoded = av_frame_alloc();
    int serial = packetQueue.getSerial();

    while (m_running.load()) {
        AVPacket* pkt = packetQueue.pop(50);
        if (!pkt) continue;
        g_stats.videoPacketsPopped++;

        int newSerial = packetQueue.getSerial();
        if (newSerial != serial) {
            // Serial changed — a flush/seek happened. Flush codec internal state.
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

            g_stats.videoFramesDecoded++;

            // Get a pre-allocated write slot from the queue (blocks if full)
            int dstLinesize;
            g_stats.decoderGetBufferCalls++;
            uint8_t* dst = frameQueue.getWriteBuffer(dstLinesize);
            if (!dst) { av_frame_unref(decoded); break; } // aborted

            // Scale directly into the queue's slot buffer
            uint8_t* dstPlanes[1] = {dst};
            int dstStrides[1] = {dstLinesize};
            g_stats.decoderSwsScaleCalls++;
            sws_scale(m_swsCtx, decoded->data, decoded->linesize,
                      0, m_height, dstPlanes, dstStrides);

            int64_t pts = decoded->pts;
            if (pts == AV_NOPTS_VALUE)
                pts = decoded->best_effort_timestamp;

            frameQueue.push(pts, serial);
            g_stats.videoFramesPushed++;
            av_frame_unref(decoded);
        }
    }

    av_frame_free(&decoded);
}
