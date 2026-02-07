#include "audio_player.h"
#include <Arduino.h>

namespace mova {

bool audioInit() {
    // TODO: Phase 5 - Initialize M5.Speaker
    return false;
}

void taskAudioPlayback(void* param) {
    (void)param;
    AudioCommand cmd;
    for (;;) {
        if (xQueueReceive(g_audioQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            Serial.printf("[Audio] Received %dHz %dbit %zu bytes (stub - discarding)\n",
                          cmd.sampleRate, cmd.bits, cmd.pcmLength);
            if (cmd.pcmData) free(cmd.pcmData);
        }
    }
}

}  // namespace mova
