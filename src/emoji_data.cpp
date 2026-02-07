#include "emoji_data.h"
#include "emoji_bitmaps.h"
#include <cstring>

namespace mova {

static bool isWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

const EmojiEntry EMOJI_TABLE[EMOJI_COUNT] = {
    { "\xF0\x9F\x98\x90", EMOJI_NEUTRAL,   sizeof(EMOJI_NEUTRAL)   },  // 😐
    { "\xF0\x9F\x98\x80", EMOJI_GRINNING,  sizeof(EMOJI_GRINNING)  },  // 😀
    { "\xF0\x9F\x98\x8E", EMOJI_COOL,      sizeof(EMOJI_COOL)      },  // 😎
    { "\xF0\x9F\x94\xA5", EMOJI_FIRE,      sizeof(EMOJI_FIRE)      },  // 🔥
    { "\xF0\x9F\x98\xA2", EMOJI_CRYING,    sizeof(EMOJI_CRYING)    },  // 😢
    { "\xF0\x9F\x98\xA1", EMOJI_ANGRY,     sizeof(EMOJI_ANGRY)     },  // 😡
    { "\xE2\x9D\xA4",     EMOJI_HEART,     sizeof(EMOJI_HEART)     },  // ❤ (VS16 stripped; input "❤️" matches via Pass 2)
    { "\xE2\x9A\xA1",     EMOJI_LIGHTNING, sizeof(EMOJI_LIGHTNING) },  // ⚡
};

size_t normalizeEmoji(const char* input, char* outBuf, size_t outBufLen) {
    if (!input || outBufLen == 0) return 0;

    while (*input && isWhitespace(*input)) input++;

    size_t len = strlen(input);

    while (len > 0 && isWhitespace(input[len - 1])) len--;

    if (len == 0 || len >= outBufLen) {
        outBuf[0] = '\0';
        return 0;
    }

    memcpy(outBuf, input, len);
    outBuf[len] = '\0';
    return len;
}

uint8_t findEmojiIndex(const char* name) {
    char normalized[32];
    if (normalizeEmoji(name, normalized, sizeof(normalized)) == 0) return EMOJI_INVALID;

    // Pass 1: exact match
    for (size_t i = 0; i < EMOJI_COUNT; i++) {
        if (strcmp(normalized, EMOJI_TABLE[i].name) == 0) return static_cast<uint8_t>(i);
    }

    // Pass 2: strip VS16 (U+FE0F = \xEF\xB8\x8F) from input and retry
    static const char vs16[] = "\xEF\xB8\x8F";
    char stripped[32];
    strncpy(stripped, normalized, sizeof(stripped) - 1);
    stripped[sizeof(stripped) - 1] = '\0';
    char* pos = strstr(stripped, vs16);
    if (pos) {
        memmove(pos, pos + 3, strlen(pos + 3) + 1);
        for (size_t i = 0; i < EMOJI_COUNT; i++) {
            if (strcmp(stripped, EMOJI_TABLE[i].name) == 0) return static_cast<uint8_t>(i);
        }
    }

    return EMOJI_INVALID;
}

const EmojiEntry* getEmoji(uint8_t index) {
    if (index >= EMOJI_COUNT) return nullptr;
    return &EMOJI_TABLE[index];
}

}  // namespace mova
