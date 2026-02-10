#ifndef MOVA_AUDIO_PLAYER_H
#define MOVA_AUDIO_PLAYER_H

#include <cstdint>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace mova {

// I2S owner mutex: shared between audio playback and mic recording.
// Speaker and mic share the same I2S port on Core S3 and cannot run simultaneously.
extern SemaphoreHandle_t g_i2sOwnerMutex;

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
