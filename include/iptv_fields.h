/* ProsperoTV - Field order and spatial deinterlacing for broadcast H.264 and MPEG-2.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "iptv_color.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct iptv_field_info
    {
        uint32_t first; /* 0: progressive/unknown, 1: top, 2: bottom. */
        uint32_t count; /* Displayed fields, including repeat-first-field. */
        uint32_t duration_us;
        uint32_t field_picture; /* One access unit contains only one field. */
        iptv_color_info_t color;
    } iptv_field_info_t;
    typedef struct iptv_field_parser iptv_field_parser_t;

    /* Codec matches the native backend's: 1 H.264, 2 HEVC, 4 MPEG-2, 5 MPEG-1. HEVC exports
     * color only. MPEG-1/2 access units are whole pictures (a field pair is one unit), read
     * from their own picture coding extensions; MPEG-1 is always progressive. */
    iptv_field_parser_t *iptv_field_parser_create(uint32_t codec);
    void iptv_field_parser_destroy(iptv_field_parser_t *parser);
    iptv_field_info_t iptv_field_parse(iptv_field_parser_t *parser, const void *data, size_t bytes);

    /* Bounded software decoder into caller-owned NV12: H.264's fallback, and the only
     * decoder for MPEG-1/2, which the console's has not. Returns 0 while buffered, 1 for a
     * progressive picture, 2 for interlaced, or a negative error. NULL/0 drains delayed
     * pictures. Dimensions must match the decoded picture. MPEG-1/2 data FFmpeg cannot
     * decode gives 0, no picture: a broadcast with a damaged packet goes on. */
    int iptv_field_decode(iptv_field_parser_t *parser, const void *data, size_t bytes, void *output,
                          size_t output_bytes, uint32_t pitch, uint32_t surface_height,
                          uint32_t width, uint32_t height);
    void iptv_field_decoder_reset(iptv_field_parser_t *parser);
    /* The display aspect ratio of the last picture iptv_field_decode returned, reduced (4:3,
     * 16:9), from its sample aspect ratio; 0:0 for square samples or none given. */
    void iptv_field_display_aspect(const iptv_field_parser_t *parser, uint32_t *num,
                                   uint32_t *den);
    /* The clock of the access unit the next iptv_field_decode call is given, carried through
     * the decoder's reordering to the picture it makes; UINT64_MAX for none. */
    void iptv_field_set_packet_pts(iptv_field_parser_t *parser, uint64_t pts_us);
    /* The clock of the last picture iptv_field_decode returned, UINT64_MAX when its unit had
     * none. A unit that made no picture (MPEG-2 skips a damaged one, or reinitialises on a new
     * sample aspect ratio) has a clock below it. */
    uint64_t iptv_field_picture_pts(const iptv_field_parser_t *parser);

    /* Copy one NV12 field to a distinct surface, interpolating only between rows
     * from that field. Never blend two moments or modify decoder reference pixels.
     * Padded columns are left untouched. field is 0 for top, 1 for bottom. */
    int iptv_field_bob(void *output, size_t output_bytes, const void *input, size_t input_bytes,
                       uint32_t pitch, uint32_t height, uint32_t width, uint32_t field);

#ifdef __cplusplus
}
#endif
