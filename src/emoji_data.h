#ifndef MOVA_EMOJI_DATA_H
#define MOVA_EMOJI_DATA_H

#include <cstdint>
#include <cstddef>

namespace mova {

struct EmojiEntry {
    const char* name;
    const uint16_t* data;   // RGB565 pixel data
    uint16_t width;
    uint16_t height;
};

// TODO: Phase 6 - Populate emoji table
constexpr size_t EMOJI_COUNT = 0;

const EmojiEntry* findEmoji(const char* name);

}  // namespace mova

#endif  // MOVA_EMOJI_DATA_H
