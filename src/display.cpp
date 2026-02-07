#include "display.h"
#include <Arduino.h>

namespace mova {

void displayStatus(const char* text) {
    // TODO: Phase 6 - Draw status text on display
    (void)text;
}

void displayEmoji(uint8_t index) {
    // TODO: Phase 6 - Draw emoji sprite on display
    (void)index;
}

void taskDisplay(void* param) {
    (void)param;
    DisplayCommand cmd;
    for (;;) {
        if (xQueueReceive(g_displayQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            Serial.printf("[Display] Received command type=%d (stub)\n", cmd.type);
        }
    }
}

}  // namespace mova
