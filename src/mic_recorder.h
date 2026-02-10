#ifndef MOVA_MIC_RECORDER_H
#define MOVA_MIC_RECORDER_H

#include <cstdint>
#include <cstddef>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace mova {

struct MicCommand {
    float    duration;    // seconds (MIC_MIN_DURATION .. MIC_MAX_DURATION)
    uint32_t sampleRate;  // Hz (16000)
    SemaphoreHandle_t doneSem;  // signaled when recording is complete
};

struct MicResult {
    bool     success;
    float    rms;              // RMS amplitude (0.0 - 1.0 normalized)
    int16_t  peak;             // peak absolute sample value
    float    actualDuration;   // actual recorded duration in seconds
    uint32_t sampleRate;
    int16_t* pcmData;          // PSRAM buffer (caller must free after use)
    size_t   pcmSamples;       // number of samples
    size_t   pcmBytes;         // number of bytes
    char     errorMsg[64];
};

bool micInit();
void taskMicRecording(void* param);

extern QueueHandle_t     g_micQueue;
extern MicResult         g_micResult;
extern SemaphoreHandle_t g_i2sOwnerMutex;

}  // namespace mova

#endif  // MOVA_MIC_RECORDER_H
