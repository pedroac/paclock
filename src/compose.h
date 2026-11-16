#ifndef COMPOSE_H
#define COMPOSE_H

#include <stdbool.h>
#include <stdint.h>

/* True if a key with this keysym may start a Compose sequence or a dead key. The compose table
 * (a few hundred kB) is built when the first such key is pressed, and not at the start. Taken from
 * the sequences in the Compose files of libX11 (62 locales, about 12,400 sequences): they start
 * with a dead key (0xfe50 to 0xfe8f), the ch keys (0xfea0 to 0xfea5), Multi_key (0xff20), a
 * Unicode keysym (from 0x01000000: Khmer and Arabic ligatures), or backslash (0x5c). The test checks
 * this against the files that are installed. */
static inline bool compose_may_start(uint32_t sym) {
    return (sym >= 0xfe50 && sym <= 0xfeff) || sym == 0xff20 || sym >= 0x01000000 || sym == 0x5c;
}

#endif
