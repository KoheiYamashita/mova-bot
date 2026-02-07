#ifndef MOVA_EMOJI_DATA_H
#define MOVA_EMOJI_DATA_H

#include <cstdint>
#include <cstddef>

namespace mova {

struct EmojiEntry {
    const char* name;            // UTF-8 emoji string (for matching)
    const uint8_t* jpegData;     // JPEG byte array in Flash
    uint32_t jpegLen;            // JPEG data length
};

constexpr size_t EMOJI_COUNT = 8;
constexpr uint8_t EMOJI_DEFAULT_INDEX = 0;  // 😐
constexpr uint8_t EMOJI_INVALID = 0xFF;

extern const EmojiEntry EMOJI_TABLE[EMOJI_COUNT];

// Trim leading/trailing whitespace from input into outBuf. Returns length, or 0 on empty/overflow.
size_t normalizeEmoji(const char* input, char* outBuf, size_t outBufLen);

uint8_t findEmojiIndex(const char* name);
const EmojiEntry* getEmoji(uint8_t index);

}  // namespace mova

#endif  // MOVA_EMOJI_DATA_H
