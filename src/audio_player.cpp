#include "audio_player.h"
#include "config.h"
#include <M5Unified.h>

namespace mova {

static bool s_speakerReady = false;

bool audioInit() {
    M5.Speaker.setVolume(SPEAKER_DEFAULT_VOLUME);
    if (!M5.Speaker.begin()) {
        Serial.println("[Audio] WARNING: Speaker.begin() failed");
        return false;
    }
    s_speakerReady = true;
    Serial.printf("[Audio] Speaker initialized (volume=%d)\n", SPEAKER_DEFAULT_VOLUME);
    return true;
}

void taskAudioPlayback(void* param) {
    (void)param;
    AudioCommand cmd;
    uint8_t* currentBuffer = nullptr;

    for (;;) {
        if (xQueueReceive(g_audioQueue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        Serial.printf("[Audio] Received %dHz %dbit %zu bytes\n",
                      cmd.sampleRate, cmd.bits, cmd.pcmLength);

        // Guard: skip playback if speaker not initialized
        if (!s_speakerReady) {
            static bool s_warnedOnce = false;
            if (!s_warnedOnce) {
                Serial.println("[Audio] Speaker not ready - discarding (further warnings suppressed)");
                s_warnedOnce = true;
            }
            if (cmd.pcmData) free(cmd.pcmData);
            continue;
        }

        // Free previous buffer if still tracked
        if (currentBuffer) {
            M5.Speaker.stop(0);
            // Wait up to 50ms for stop to take effect
            for (int i = 0; i < 5 && M5.Speaker.isPlaying(0) != 0; ++i) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (M5.Speaker.isPlaying(0) != 0) {
                Serial.println("[Audio] Force-freeing buffer after stop timeout");
            }
            free(currentBuffer);
            currentBuffer = nullptr;
        }

        if (!cmd.pcmData || cmd.pcmLength == 0) {
            continue;
        }

        currentBuffer = cmd.pcmData;

        // Call the appropriate playRaw overload based on bit depth
        bool ok;
        if (cmd.bits == 16) {
            ok = M5.Speaker.playRaw(reinterpret_cast<const int16_t*>(cmd.pcmData),
                                    cmd.pcmLength / 2, cmd.sampleRate,
                                    false, 1, 0, true);
        } else {
            ok = M5.Speaker.playRaw(cmd.pcmData, cmd.pcmLength,
                                    cmd.sampleRate, false, 1, 0, true);
        }
        if (!ok) {
            Serial.println("[Audio] playRaw() failed");
            free(currentBuffer);
            currentBuffer = nullptr;
            continue;
        }

        // Poll until playback completes or a new command arrives
        while (M5.Speaker.isPlaying(0) != 0) {
            // Check if a new command is waiting in the queue
            AudioCommand peek;
            if (xQueuePeek(g_audioQueue, &peek, 0) == pdTRUE) {
                M5.Speaker.stop(0);
                break;  // Next iteration will receive the new command
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Free buffer after playback completes (or was interrupted)
        if (currentBuffer && M5.Speaker.isPlaying(0) == 0) {
            free(currentBuffer);
            currentBuffer = nullptr;
        }
    }
}

}  // namespace mova
