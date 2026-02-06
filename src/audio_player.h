#ifndef MOVA_AUDIO_PLAYER_H
#define MOVA_AUDIO_PLAYER_H

#include <cstdint>

namespace mova {

struct AudioCommand {
    uint16_t frequency;   // Hz (0 = stop)
    uint16_t durationMs;
};

bool audioInit();
void taskAudioPlayback(void* param);

}  // namespace mova

#endif  // MOVA_AUDIO_PLAYER_H
