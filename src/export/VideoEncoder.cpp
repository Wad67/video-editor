#include "export/VideoEncoder.h"
#include <cmath>
#include <cstdio>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

VideoEncoder::~VideoEncoder() {
    shutdown();
}

bool VideoEncoder::init(const ExportSettings& settings, int muxerFlags) {
    m_width = settings.width;
    m_height = settings.height;

    // Select codec
    const char* codecName = nullptr;
    switch (settings.videoCodec) {
        case VideoCodecChoice::H264_Software: codecName = "libx264"; break;
        case VideoCodecChoice::H265_Software: codecName = "libx265"; break;
        case VideoCodecChoice::H264_VAAPI:    codecName = "h264_vaapi"; break;
    }

    const AVCodec* codec = avcodec_find_encoder_by_name(codecName);
    if (!codec) {
        fprintf(stderr, "VideoEncoder: codec '%s' not found\n", codecName);
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        fprintf(stderr, "VideoEncoder: cannot allocate codec context\n");
        return false;
    }

    m_codecCtx->width = settings.width;
    m_codecCtx->height = settings.height;
    // time_base = 1/fps so that PTS=frameIndex gives correct timing.
    // For 29.97 fps: use 1001/30000. For integer fps: 1/fps.
    int fpsInt = static_cast<int>(settings.fps + 0.5);
    if (std::abs(settings.fps - 29.97) < 0.1) {
        m_codecCtx->time_base = AVRational{1001, 30000};
        m_codecCtx->framerate = AVRational{30000, 1001};
    } else if (std::abs(settings.fps - 23.976) < 0.1) {
        m_codecCtx->time_base = AVRational{1001, 24000};
        m_codecCtx->framerate = AVRational{24000, 1001};
    } else {
        m_codecCtx->time_base = AVRational{1, fpsInt};
        m_codecCtx->framerate = AVRational{fpsInt, 1};
    }
    m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_codecCtx->gop_size = 12;
    m_codecCtx->max_b_frames = 2;

    // MP4 container needs global header
    if (muxerFlags & AVFMT_GLOBALHEADER) {
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Codec-specific options
    if (settings.videoCodec == VideoCodecChoice::H264_Software) {
        av_opt_set(m_codecCtx->priv_data, "crf",
                   std::to_string(settings.crf).c_str(), 0);
        av_opt_set(m_codecCtx->priv_data, "preset", "medium", 0);
    } else if (settings.videoCodec == VideoCodecChoice::H265_Software) {
        av_opt_set(m_codecCtx->priv_data, "crf",
                   std::to_string(settings.crf).c_str(), 0);
        av_opt_set(m_codecCtx->priv_data, "preset", "medium", 0);
    } else {
        // VAAPI: use bitrate mode
        m_codecCtx->bit_rate = settings.videoBitrate;
    }

    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "VideoEncoder: cannot open codec: %s\n", errbuf);
        avcodec_free_context(&m_codecCtx);
        return false;
    }

    // Setup sws for RGBA -> YUV420P
    m_swsCtx = sws_getContext(
        settings.width, settings.height, AV_PIX_FMT_RGBA,
        settings.width, settings.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        fprintf(stderr, "VideoEncoder: cannot create sws context\n");
        shutdown();
        return false;
    }

    // Allocate YUV frame
    m_frame = av_frame_alloc();
    m_frame->format = AV_PIX_FMT_YUV420P;
    m_frame->width = settings.width;
    m_frame->height = settings.height;
    ret = av_frame_get_buffer(m_frame, 0);
    if (ret < 0) {
        fprintf(stderr, "VideoEncoder: cannot allocate frame buffer\n");
        shutdown();
        return false;
    }

    fprintf(stderr, "[EXPORT] Video encoder: %s %dx%d @ %.1f fps\n",
            codecName, settings.width, settings.height, settings.fps);
    return true;
}

void VideoEncoder::shutdown() {
    if (m_frame) {
        av_frame_free(&m_frame);
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
}

bool VideoEncoder::encodeFrame(const uint8_t* rgbaData, int width, int height,
                                int64_t frameIndex, PacketCallback cb) {
    if (!m_codecCtx || !m_swsCtx || !m_frame) return false;

    av_frame_make_writable(m_frame);

    // Convert RGBA -> YUV420P
    const uint8_t* srcSlice[1] = { rgbaData };
    int srcStride[1] = { width * 4 };
    sws_scale(m_swsCtx, srcSlice, srcStride, 0, height,
              m_frame->data, m_frame->linesize);

    m_frame->pts = frameIndex;

    int ret = avcodec_send_frame(m_codecCtx, m_frame);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        fprintf(stderr, "VideoEncoder: send_frame failed: %s\n", errbuf);
        return false;
    }

    return drainPackets(cb);
}

bool VideoEncoder::flush(PacketCallback cb) {
    if (!m_codecCtx) return false;

    avcodec_send_frame(m_codecCtx, nullptr);
    return drainPackets(cb);
}

bool VideoEncoder::drainPackets(PacketCallback& cb) {
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
