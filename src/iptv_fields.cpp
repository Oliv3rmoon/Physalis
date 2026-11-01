// ProsperoTV - Use the bundled H.264 parser; reconstruct each field separately. MPEG-1/2
// pictures are read from their own headers and decoded in software.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "iptv_fields.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/rational.h>
}

namespace
{
// The native backend's codec numbers (iptv_native_backend.h).
constexpr uint32_t kH264 = 1, kHevc = 2, kMpeg2 = 4, kMpeg1 = 5;

AVCodecID codec_id(uint32_t codec)
{
    switch (codec)
    {
    case kH264:
        return AV_CODEC_ID_H264;
    case kHevc:
        return AV_CODEC_ID_HEVC;
    case kMpeg2:
        return AV_CODEC_ID_MPEG2VIDEO;
    case kMpeg1:
        return AV_CODEC_ID_MPEG1VIDEO;
    default:
        return AV_CODEC_ID_NONE;
    }
}

bool mpeg(AVCodecID id)
{
    return id == AV_CODEC_ID_MPEG2VIDEO || id == AV_CODEC_ID_MPEG1VIDEO;
}
} // namespace

struct iptv_field_parser
{
    AVCodecParserContext *parser = nullptr;
    AVCodecContext *codec = avcodec_alloc_context3(nullptr);
    AVCodecContext *decoder = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    bool draining = false;
    std::uint8_t *padded = nullptr;
    unsigned padded_capacity = 0;
    // MPEG-1/2: what the last sequence header and its extensions said, for the pictures that
    // follow without them.
    bool progressive_sequence = true;
    AVRational frame_rate{0, 1};
    iptv_color_info_t color{2, 2, 2, 1, {}};
    // The last decoded picture's sample aspect ratio, size and clock; the next unit's clock.
    AVRational sample_aspect{0, 1};
    int picture_width = 0, picture_height = 0;
    uint64_t picture_pts = UINT64_MAX, next_pts = UINT64_MAX;
    ~iptv_field_parser()
    {
        av_parser_close(parser);
        avcodec_free_context(&codec);
        avcodec_free_context(&decoder);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_free(padded);
    }
};

iptv_field_parser_t *iptv_field_parser_create(uint32_t codec)
{
    const AVCodecID id = codec_id(codec);
    if (id == AV_CODEC_ID_NONE)
        return nullptr;
    auto *state = new (std::nothrow) iptv_field_parser;
    // MPEG-1/2 pictures are read below, from their own headers.
    if (state && !mpeg(id))
        state->parser = av_parser_init(id);
    if (state && (state->parser || mpeg(id)) && state->codec)
    {
        if (state->parser)
            state->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
        state->codec->codec_id = id;
        state->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        return state;
    }
    delete state;
    return nullptr;
}

namespace
{
// One MPEG-1/2 access unit (ISO/IEC 13818-2 6.2): its sequence header and extensions update
// what the parser keeps; its first picture coding extension says how its picture is shown.
// A field pair is one unit, its first field's extension coming first.
iptv_field_info_t mpeg_fields(iptv_field_parser_t *state, const std::uint8_t *data, size_t bytes)
{
    static const AVRational rates[9] = {{0, 1},  {24000, 1001}, {24, 1},
                                        {25, 1}, {30000, 1001}, {30, 1},
                                        {50, 1}, {60000, 1001}, {60, 1}};
    iptv_field_info_t result{};
    bool picture = false, coded = false;
    unsigned structure = 3, top_first = 0, progressive_frame = 1;
    for (size_t at = 0; at + 4 <= bytes; ++at)
    {
        if (data[at] || data[at + 1] || data[at + 2] != 1)
            continue;
        const std::uint8_t code = data[at + 3];
        const std::uint8_t *unit = data + at + 4;
        const size_t left = bytes - at - 4;
        if (code == 0xb3 && left >= 4)
        {
            state->frame_rate = rates[(unit[3] & 0x0f) < 9 ? unit[3] & 0x0f : 0];
            state->progressive_sequence = true; // until an extension says otherwise
        }
        else if (code == 0x00)
            picture = true;
        else if (code == 0xb5 && left >= 1)
        {
            const unsigned id = unit[0] >> 4;
            if (id == 1 && left >= 6)
            {
                state->progressive_sequence = (unit[1] & 0x08) != 0;
                const int n = ((unit[5] >> 5) & 3) + 1, d = (unit[5] & 0x1f) + 1;
                if (state->frame_rate.num > 0)
                    state->frame_rate = av_mul_q(state->frame_rate, AVRational{n, d});
            }
            else if (id == 2 && left >= 1 && (unit[0] & 1) && left >= 4)
            {
                // Sequence display extension: video_format 3 bits, colour_description.
                state->color.primaries = unit[1];
                state->color.transfer = unit[2];
                state->color.matrix = unit[3];
            }
            else if (id == 8 && left >= 5 && picture && !coded)
            {
                coded = true;
                structure = unit[2] & 3;
                top_first = unit[3] >> 7;
                progressive_frame = unit[4] >> 7;
            }
        }
    }
    result.color = state->color;
    if (!picture)
        return result;
    if (structure == 1 || structure == 2)
    {
        // Two field pictures in one unit: the decoder makes one frame of them.
        result.first = structure;
        result.count = 2;
    }
    else if (coded && !state->progressive_sequence && !progressive_frame)
    {
        result.first = top_first ? 1u : 2u;
        result.count = 2;
    }
    const auto rate = state->frame_rate;
    if (result.first && rate.num > 0 && rate.den > 0)
    {
        const auto duration = UINT64_C(1000000) * rate.den / (UINT64_C(2) * rate.num);
        if (duration >= 1000 && duration <= 1000000)
            result.duration_us = static_cast<std::uint32_t>(duration);
    }
    return result;
}
} // namespace
void iptv_field_parser_destroy(iptv_field_parser_t *parser)
{
    delete parser;
}
iptv_field_info_t iptv_field_parse(iptv_field_parser_t *state, const void *data, size_t bytes)
{
    iptv_field_info_t result{};
    if (!state || !data || !bytes || bytes > 8u * 1024u * 1024u)
        return result;
    if (mpeg(state->codec->codec_id))
        return mpeg_fields(state, static_cast<const std::uint8_t *>(data), bytes);
    av_fast_padded_malloc(&state->padded, &state->padded_capacity, bytes);
    if (!state->padded)
        return result;
    std::memcpy(state->padded, data, bytes);
    std::uint8_t *parsed = nullptr;
    int parsed_bytes = 0;
    state->codec->color_primaries = AVCOL_PRI_UNSPECIFIED;
    state->codec->color_trc = AVCOL_TRC_UNSPECIFIED;
    state->codec->colorspace = AVCOL_SPC_UNSPECIFIED;
    state->codec->color_range = AVCOL_RANGE_UNSPECIFIED;
    if (av_parser_parse2(state->parser, state->codec, &parsed, &parsed_bytes, state->padded,
                         static_cast<int>(bytes), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0) < 0 ||
        !parsed_bytes)
        return result;
    result.color = {static_cast<uint32_t>(state->codec->color_primaries),
                    static_cast<uint32_t>(state->codec->color_trc),
                    static_cast<uint32_t>(state->codec->colorspace),
                    static_cast<uint32_t>(state->codec->color_range),
                    {}};
    const auto side = [&](AVPacketSideDataType type)
    {
        return av_packet_side_data_get(state->codec->coded_side_data,
                                       state->codec->nb_coded_side_data, type);
    };
    if (const auto *sd = side(AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
        sd && sd->size >= sizeof(AVMasteringDisplayMetadata))
    {
        const auto &m = *reinterpret_cast<const AVMasteringDisplayMetadata *>(sd->data);
        const auto coordinate = [](AVRational v)
        { return static_cast<uint16_t>(std::clamp(av_q2d(v) * 50000, 0.0, 50000.0) + .5); };
        if (m.has_primaries && m.has_luminance && m.min_luminance.den > 0 &&
            m.max_luminance.den > 0 && av_q2d(m.min_luminance) >= 0 &&
            av_q2d(m.max_luminance) > av_q2d(m.min_luminance) && av_q2d(m.max_luminance) <= 10000)
        {
            auto &hdr = result.color.hdr;
            hdr.flags |= 1;
            for (unsigned c = 0; c < 3; ++c)
                for (unsigned xy = 0; xy < 2; ++xy)
                    hdr.primaries[c][xy] = coordinate(m.display_primaries[c][xy]);
            for (unsigned xy = 0; xy < 2; ++xy)
                hdr.white[xy] = coordinate(m.white_point[xy]);
            hdr.min_luminance = static_cast<uint32_t>(av_q2d(m.min_luminance) * 10000 + .5);
            hdr.max_luminance = static_cast<uint32_t>(av_q2d(m.max_luminance) * 10000 + .5);
        }
    }
    if (const auto *sd = side(AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
        sd && sd->size >= sizeof(AVContentLightMetadata))
    {
        const auto &m = *reinterpret_cast<const AVContentLightMetadata *>(sd->data);
        if (m.MaxCLL <= 65535 && m.MaxFALL <= 65535)
        {
            result.color.hdr.flags |= 2;
            result.color.hdr.max_cll = static_cast<uint16_t>(m.MaxCLL);
            result.color.hdr.max_fall = static_cast<uint16_t>(m.MaxFALL);
        }
    }
    if (state->codec->codec_id != AV_CODEC_ID_H264)
        return result;
    const auto structure = state->parser->picture_structure;
    const auto order = state->parser->field_order;
    if (structure == AV_PICTURE_STRUCTURE_TOP_FIELD ||
        structure == AV_PICTURE_STRUCTURE_BOTTOM_FIELD)
    {
        result.first = structure == AV_PICTURE_STRUCTURE_TOP_FIELD ? 1u : 2u;
        result.field_picture = 1;
        result.count = 2; // The native decoder combines the pair into one surface.
    }
    else if (order == AV_FIELD_TT || order == AV_FIELD_TB || order == AV_FIELD_BB ||
             order == AV_FIELD_BT)
    {
        result.first = order == AV_FIELD_TT || order == AV_FIELD_BT ? 1u : 2u;
        result.count = static_cast<unsigned>(std::clamp(state->parser->repeat_pict + 1, 2, 6));
    }
    const auto rate = state->codec->framerate;
    if (result.first && rate.num > 0 && rate.den > 0)
    {
        const auto duration = UINT64_C(1000000) * rate.den / (UINT64_C(2) * rate.num);
        if (duration >= 1000 && duration <= 1000000)
            result.duration_us = static_cast<std::uint32_t>(duration);
    }
    return result;
}

int iptv_field_decode(iptv_field_parser_t *state, const void *data, size_t bytes, void *output,
                      size_t output_bytes, uint32_t pitch, uint32_t surface_height, uint32_t width,
                      uint32_t height)
{
    if (!state || (state->codec->codec_id != AV_CODEC_ID_H264 && !mpeg(state->codec->codec_id)) ||
        !output || (bytes && !data) || bytes > 8u * 1024u * 1024u || !width || !height ||
        width > 1920 || height > 1088 || (width & 1u) || (height & 1u) || pitch < width ||
        pitch > 2048 || (pitch & 1u) || surface_height < height || surface_height > 1088 ||
        (surface_height & 1u) || output_bytes < static_cast<size_t>(pitch) * surface_height * 3 / 2)
        return AVERROR(EINVAL);
    if (!state->decoder)
    {
        const bool mpeg_video = mpeg(state->codec->codec_id);
        const auto *codec = avcodec_find_decoder(state->codec->codec_id);
        if (!codec)
            return AVERROR_DECODER_NOT_FOUND;
        state->decoder = avcodec_alloc_context3(codec);
        state->packet = av_packet_alloc();
        state->frame = av_frame_alloc();
        if (!state->decoder || !state->packet || !state->frame)
            return AVERROR(ENOMEM);
        state->decoder->thread_count = 4;
        // FFmpeg's MPEG-1/2 decoder threads by slices, and conceals a damaged slice from the
        // pictures around it: live broadcasts lose packets, and a picture goes on.
        state->decoder->thread_type = mpeg_video ? FF_THREAD_SLICE : FF_THREAD_FRAME;
        state->decoder->max_pixels = 1920 * 1088;
        if (!mpeg_video)
            state->decoder->err_recognition = AV_EF_EXPLODE;
        const int result = avcodec_open2(state->decoder, codec, nullptr);
        if (result < 0)
            return result;
    }
    const bool mpeg_video = mpeg(state->codec->codec_id);
    if (bytes)
    {
        if (state->draining)
            return AVERROR_EOF;
        av_packet_unref(state->packet);
        int result = av_new_packet(state->packet, static_cast<int>(bytes));
        if (result < 0)
            return result;
        std::memcpy(state->packet->data, data, bytes);
        if (state->next_pts <= static_cast<uint64_t>(INT64_MAX))
            state->packet->pts = static_cast<int64_t>(state->next_pts);
        state->next_pts = UINT64_MAX;
        result = avcodec_send_packet(state->decoder, state->packet);
        if (result == AVERROR_INVALIDDATA && mpeg_video)
            return 0;
        if (result < 0)
            return result;
    }
    else if (!state->draining)
    {
        const int result = avcodec_send_packet(state->decoder, nullptr);
        if (result < 0)
            return result;
        state->draining = true;
    }
    av_frame_unref(state->frame);
    const int result = avcodec_receive_frame(state->decoder, state->frame);
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF ||
        (result == AVERROR_INVALIDDATA && mpeg_video))
        return 0;
    if (result < 0)
        return result;
    const auto &frame = *state->frame;
    if (frame.format != AV_PIX_FMT_YUV420P || frame.width != static_cast<int>(width) ||
        frame.height != static_cast<int>(height) || frame.linesize[0] < frame.width ||
        frame.linesize[1] < frame.width / 2 || frame.linesize[2] < frame.width / 2)
        return mpeg_video ? 0 : AVERROR_INVALIDDATA;
    state->picture_pts = frame.pts != AV_NOPTS_VALUE && frame.pts >= 0
                             ? static_cast<uint64_t>(frame.pts)
                             : UINT64_MAX;
    state->sample_aspect = frame.sample_aspect_ratio;
    state->picture_width = frame.width;
    state->picture_height = frame.height;
    auto *y = static_cast<std::uint8_t *>(output);
    auto *uv = y + static_cast<size_t>(pitch) * surface_height;
    std::memset(y, 16, static_cast<size_t>(pitch) * surface_height);
    std::memset(uv, 128, static_cast<size_t>(pitch) * surface_height / 2);
    for (unsigned row = 0; row < height; ++row)
        std::memcpy(y + static_cast<size_t>(row) * pitch,
                    frame.data[0] + static_cast<size_t>(row) * frame.linesize[0], width);
    for (unsigned row = 0; row < height / 2; ++row)
        for (unsigned x = 0; x < width / 2; ++x)
        {
            uv[static_cast<size_t>(row) * pitch + x * 2] =
                frame.data[1][static_cast<size_t>(row) * frame.linesize[1] + x];
            uv[static_cast<size_t>(row) * pitch + x * 2 + 1] =
                frame.data[2][static_cast<size_t>(row) * frame.linesize[2] + x];
        }
    return (frame.flags & AV_FRAME_FLAG_INTERLACED) ? 2 : 1;
}

void iptv_field_display_aspect(const iptv_field_parser_t *state, uint32_t *num, uint32_t *den)
{
    if (!num || !den)
        return;
    *num = *den = 0;
    if (!state || state->sample_aspect.num <= 0 || state->sample_aspect.den <= 0 ||
        state->sample_aspect.num == state->sample_aspect.den || state->picture_width <= 0 ||
        state->picture_height <= 0)
        return;
    int reduced_num = 0, reduced_den = 0;
    (void)av_reduce(&reduced_num, &reduced_den,
                    static_cast<std::int64_t>(state->sample_aspect.num) * state->picture_width,
                    static_cast<std::int64_t>(state->sample_aspect.den) * state->picture_height,
                    10000);
    if (reduced_num > 0 && reduced_den > 0)
    {
        *num = static_cast<uint32_t>(reduced_num);
        *den = static_cast<uint32_t>(reduced_den);
    }
}

void iptv_field_set_packet_pts(iptv_field_parser_t *state, uint64_t pts_us)
{
    if (state)
        state->next_pts = pts_us;
}

uint64_t iptv_field_picture_pts(const iptv_field_parser_t *state)
{
    return state ? state->picture_pts : UINT64_MAX;
}

void iptv_field_decoder_reset(iptv_field_parser_t *state)
{
    if (state && state->decoder)
    {
        avcodec_flush_buffers(state->decoder);
        state->draining = false;
    }
}

int iptv_field_bob(void *output, size_t output_bytes, const void *input, size_t input_bytes,
                   uint32_t pitch, uint32_t height, uint32_t width, uint32_t field)
{
    if (!output || !input || field > 1 || !width || width > pitch || (width & 1u) || (pitch & 1u) ||
        height < 4 || (height & 3u) || pitch > 8192 || height > 8192)
        return -1;
    const size_t required = static_cast<size_t>(pitch) * (height + height / 2);
    const auto dst_address = reinterpret_cast<uintptr_t>(output);
    const auto src_address = reinterpret_cast<uintptr_t>(input);
    if (required > output_bytes || required > input_bytes || required > UINTPTR_MAX - dst_address ||
        required > UINTPTR_MAX - src_address ||
        (dst_address < src_address + required && src_address < dst_address + required))
        return -1;
    auto *dst = static_cast<std::uint8_t *>(output);
    const auto *src = static_cast<const std::uint8_t *>(input);
    // ponytail: spatial bob preserves field-rate motion at half vertical detail;
    // a motion-adaptive filter can recover detail if its measured cost allows it.
    for (const auto rows : {height, height / 2})
    {
        for (std::uint32_t y = 0; y < rows; ++y)
        {
            auto *row = dst + static_cast<size_t>(y) * pitch;
            if ((y & 1u) == field)
                std::memcpy(row, src + static_cast<size_t>(y) * pitch, width);
            else
            {
                const auto above = y ? y - 1 : 1;
                const auto below = y + 1 < rows ? y + 1 : y - 1;
                const auto *a = src + static_cast<size_t>(above) * pitch;
                const auto *b = src + static_cast<size_t>(below) * pitch;
                for (std::uint32_t x = 0; x < width; ++x)
                    row[x] = static_cast<std::uint8_t>((a[x] + b[x] + 1u) / 2);
            }
        }
        dst += static_cast<size_t>(rows) * pitch;
        src += static_cast<size_t>(rows) * pitch;
    }
    return 0;
}
