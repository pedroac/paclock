#include "pamtext.h"
#include <security/pam_appl.h>
#include <stdio.h>
#include <string.h>

void pamtext_add(char *info, size_t size, int style, const char *text) {
    if (size == 0 || !text || style != PAM_ERROR_MSG) return;
    size_t n = strlen(info);
    if (n && n + 2 < size) {
        info[n++] = ' ';
        info[n] = '\0';
    }
    for (; *text && n + 1 < size; text++)
        info[n++] = (unsigned char)*text < 0x20 || *text == 0x7f ? ' ' : *text;
    info[n] = '\0';
}

bool pamtext_hint(const char *info, char *out, size_t size) {
    if (size == 0) return false;
    out[0] = '\0';
    const char *close = strrchr(info, ')');
    if (!close) return false;
    const char *open = NULL;
    for (const char *p = info; p < close; p++)
        if (*p == '(') open = p;
    if (!open || close - open < 2) return false;
    size_t len = (size_t)(close - open - 1);
    if (len >= size) len = size - 1;
    memcpy(out, open + 1, len);
    out[len] = '\0';
    return out[0] != '\0';
}
