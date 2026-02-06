#ifndef MOVA_DISPLAY_H
#define MOVA_DISPLAY_H

#include <cstdint>

namespace mova {

struct DisplayCommand {
    enum Type : uint8_t {
        STATUS,
        EMOJI,
    };
    Type type;
    uint8_t emojiIndex;
    char statusText[32];
};

void displayStatus(const char* text);
void displayEmoji(uint8_t index);
void taskDisplay(void* param);

}  // namespace mova

#endif  // MOVA_DISPLAY_H
