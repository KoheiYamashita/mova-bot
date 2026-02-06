#include "audio_player.h"

namespace mova {

bool audioInit() {
    // TODO: Phase 5 - Initialize M5.Speaker
    return false;
}

void taskAudioPlayback(void* param) {
    // TODO: Phase 5 - FreeRTOS task: receive AudioCommand from queue, play tones
    (void)param;
}

}  // namespace mova
