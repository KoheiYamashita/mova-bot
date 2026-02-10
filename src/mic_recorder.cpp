#include "mic_recorder.h"
#include "config.h"
#include <M5Unified.h>
#include <cmath>

namespace mova {

QueueHandle_t     g_micQueue      = nullptr;
MicResult         g_micResult     = {};
SemaphoreHandle_t g_i2sOwnerMutex = nullptr;

bool micInit() {
    g_i2sOwnerMutex = xSemaphoreCreateMutex();
    if (!g_i2sOwnerMutex) {
        Serial.println("[Mic] ERROR: I2S mutex alloc failed");
        return false;
    }

    g_micQueue = xQueueCreate(QUEUE_SIZE_MIC, sizeof(MicCommand));
    if (!g_micQueue) {
        Serial.println("[Mic] ERROR: Mic queue alloc failed");
        return false;
    }

    Serial.println("[Mic] Initialized");
    return true;
}

void taskMicRecording(void* param) {
    (void)param;
    MicCommand cmd;

    for (;;) {
        if (xQueueReceive(g_micQueue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        Serial.printf("[Mic] Recording %.1fs at %luHz\n", cmd.duration, cmd.sampleRate);

        // Acquire I2S ownership (blocks until speaker releases)
        if (xSemaphoreTake(g_i2sOwnerMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
            g_micResult.success = false;
            snprintf(g_micResult.errorMsg, sizeof(g_micResult.errorMsg),
                     "I2S busy (speaker timeout)");
            xSemaphoreGive(cmd.doneSem);
            continue;
        }

        // Stop speaker to free I2S port (shared BCK/WS pins)
        M5.Speaker.stop();
        M5.Speaker.end();

        // Allocate PSRAM buffer for recording
        size_t numSamples = static_cast<size_t>(cmd.duration * cmd.sampleRate);
        int16_t* buffer = static_cast<int16_t*>(ps_malloc(numSamples * sizeof(int16_t)));
        if (!buffer) {
            g_micResult.success = false;
            snprintf(g_micResult.errorMsg, sizeof(g_micResult.errorMsg),
                     "PSRAM alloc failed (%zu bytes)", numSamples * sizeof(int16_t));
            M5.Speaker.begin();
            xSemaphoreGive(g_i2sOwnerMutex);
            xSemaphoreGive(cmd.doneSem);
            continue;
        }

        // Start microphone
        auto micCfg = M5.Mic.config();
        micCfg.sample_rate = cmd.sampleRate;
        micCfg.dma_buf_count = 8;
        micCfg.dma_buf_len = 256;
        M5.Mic.config(micCfg);

        if (!M5.Mic.begin()) {
            g_micResult.success = false;
            snprintf(g_micResult.errorMsg, sizeof(g_micResult.errorMsg),
                     "Mic.begin() failed");
            free(buffer);
            M5.Speaker.begin();
            xSemaphoreGive(g_i2sOwnerMutex);
            xSemaphoreGive(cmd.doneSem);
            continue;
        }

        // Record in chunks
        size_t chunkSize = cmd.sampleRate / 10;  // 100ms chunks
        size_t recorded = 0;

        while (recorded < numSamples) {
            size_t remaining = numSamples - recorded;
            size_t thisChunk = (remaining < chunkSize) ? remaining : chunkSize;

            if (M5.Mic.record(&buffer[recorded], thisChunk, cmd.sampleRate)) {
                // Wait for this chunk to complete
                while (M5.Mic.isRecording()) {
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
                recorded += thisChunk;
            } else {
                Serial.printf("[Mic] record() failed at sample %zu\n", recorded);
                break;
            }
        }

        M5.Mic.end();

        // Restore speaker
        M5.Speaker.begin();

        // Compute RMS and peak
        int64_t sumSquares = 0;
        int16_t peak = 0;
        for (size_t i = 0; i < recorded; i++) {
            int32_t s = buffer[i];
            sumSquares += s * s;
            int16_t absS = (s < 0) ? static_cast<int16_t>(-s) : s;
            if (absS > peak) peak = absS;
        }
        float rms = (recorded > 0)
            ? sqrtf(static_cast<float>(sumSquares) / recorded) / 32768.0f
            : 0.0f;

        // Fill result
        g_micResult.success        = true;
        g_micResult.rms            = rms;
        g_micResult.peak           = peak;
        g_micResult.actualDuration = static_cast<float>(recorded) / cmd.sampleRate;
        g_micResult.sampleRate     = cmd.sampleRate;
        g_micResult.pcmData        = buffer;
        g_micResult.pcmSamples     = recorded;
        g_micResult.pcmBytes       = recorded * sizeof(int16_t);
        g_micResult.errorMsg[0]    = '\0';

        Serial.printf("[Mic] Recorded %zu samples, rms=%.4f, peak=%d\n",
                      recorded, rms, peak);

        xSemaphoreGive(g_i2sOwnerMutex);
        xSemaphoreGive(cmd.doneSem);
    }
}

}  // namespace mova
