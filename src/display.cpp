#include "display.h"
#include "emoji_data.h"
#include <M5Unified.h>

namespace mova {

void displayStatus(const char* text) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(text, M5.Display.width() / 2, M5.Display.height() / 2);
}

void displayEmoji(uint8_t index) {
    const EmojiEntry* entry = getEmoji(index);
    if (!entry) entry = getEmoji(EMOJI_DEFAULT_INDEX);
    if (!entry) return;
    M5.Display.drawJpg(entry->jpegData, entry->jpegLen, 0, 0);
}

void taskDisplay(void* param) {
    (void)param;
    DisplayCommand cmd;
    for (;;) {
        if (xQueueReceive(g_displayQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {
                case DisplayCommand::STATUS:
                    displayStatus(cmd.statusText);
                    break;
                case DisplayCommand::EMOJI:
                    displayEmoji(cmd.emojiIndex);
                    break;
                default:
                    break;
            }
        }
    }
}

}  // namespace mova
