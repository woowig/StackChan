/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "board/hal_bridge.h"
#include <aquestalk.h>
#include <mooncake_log.h>
#include <vector>

static const std::string_view _tag = "HAL-TTS";

namespace {
// AquesTalk pico always synthesizes at 8kHz; AqResample_Conv upsamples 3x to
// match this board's AUDIO_OUTPUT_SAMPLE_RATE (24kHz, see config.h).
constexpr int kAqSampleRate = 8000;
constexpr int kLenFrame     = 32;  // samples generated per SyntheFrame() call

uint32_t _aq_workbuf[AQ_SIZE_WORKBUF];
bool _aq_initialized = false;

bool _ensure_initialized()
{
    if (_aq_initialized) {
        return true;
    }
    // No license key: runs as the evaluation build (na-row/ma-row all
    // pronounced "nu"). See firmware/components/aquestalk/THIRD_PARTY_README.txt.
    uint8_t err = CAqTkPicoF_Init(_aq_workbuf, kLenFrame, nullptr);
    if (err) {
        mclog::tagError(_tag, "CAqTkPicoF_Init failed: {}", err);
        return false;
    }
    _aq_initialized = true;
    mclog::tagInfo(_tag, "AquesTalk initialized (evaluation build)");
    return true;
}
}  // namespace

bool Hal::speakSymbols(std::string_view koe)
{
    if (!_ensure_initialized()) {
        return false;
    }

    std::string koe_str(koe);  // CAqTkPicoF_SetKoe expects a NUL-terminated buffer
    uint8_t err = CAqTkPicoF_SetKoe(reinterpret_cast<const uint8_t*>(koe_str.c_str()), 100, 0xFFFFu);
    if (err) {
        mclog::tagError(_tag, "CAqTkPicoF_SetKoe failed: {}", err);
        return false;
    }

    AqResample_Reset();

    std::vector<int16_t> pcm;
    pcm.reserve(kAqSampleRate * 3);  // ~1s headroom at 24kHz, grows as needed

    int16_t frame[kLenFrame];
    int16_t resampled[3];
    while (true) {
        uint16_t len   = 0;
        uint8_t status = CAqTkPicoF_SyntheFrame(frame, &len);
        for (uint16_t i = 0; i < len; i++) {
            AqResample_Conv(frame[i], resampled);
            pcm.insert(pcm.end(), resampled, resampled + 3);
        }
        if (status != 0) {
            // 1 == end of data, anything else == error; either way, nothing more to render
            if (status != 1) {
                mclog::tagError(_tag, "CAqTkPicoF_SyntheFrame error: {}", status);
                return false;
            }
            break;
        }
    }

    mclog::tagInfo(_tag, "speaking {} samples ({:.1f}s)", pcm.size(), pcm.size() / (kAqSampleRate * 3.0f));
    hal_bridge::board_output_pcm(pcm);
    return true;
}
