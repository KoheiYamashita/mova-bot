#ifndef MOVA_AUDIO_PLAYER_H
#define MOVA_AUDIO_PLAYER_H

#include <cstdint>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace mova {

struct AudioCommand {
    uint16_t sampleRate;  // Hz (8000, 16000, 44100)
    uint8_t  bits;        // 8 or 16
    uint8_t  channels;    // 1 (mono)
    uint8_t* pcmData;     // PSRAM ポインタ (Web サーバーが ps_malloc、消費タスクが free)
    size_t   pcmLength;   // バイト数
};

bool audioInit();
void taskAudioPlayback(void* param);

extern QueueHandle_t g_audioQueue;

}  // namespace mova

#endif  // MOVA_AUDIO_PLAYER_H
