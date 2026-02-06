#include "display.h"

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
    // TODO: Phase 6 - FreeRTOS task: receive DisplayCommand from queue, update screen
    (void)param;
}

}  // namespace mova
