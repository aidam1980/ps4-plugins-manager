#include "app_state.h"
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <orbis/libkernel.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
void CopyText(char *dst, size_t size, const char *src)
{
    if (!dst || size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, size, "%s", src);
}

void Trim(char *text)
{
    if (!text) return;
    char *start = text;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1);
    size_t len = strlen(text);
    while (len && isspace((unsigned char)text[len - 1])) text[--len] = '\0';
}

uint16_t ReadLE16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t ReadLE32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool FileExists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

bool DirectoryExists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool EndsWithPluginExtension(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    if (!dot) return false;
    char ext[8];
    CopyText(ext, sizeof(ext), dot);
    for (int i = 0; ext[i]; ++i) ext[i] = (char)tolower((unsigned char)ext[i]);
    return strcmp(ext, ".prx") == 0 || strcmp(ext, ".sprx") == 0;
}

bool IsSectionLine(const char *raw, char *section, size_t sectionSize)
{
    char line[CONFIG_LINE_SIZE];
    CopyText(line, sizeof(line), raw);
    Trim(line);
    if (line[0] != '[') return false;
    char *end = strchr(line + 1, ']');
    if (!end) return false;
    *end = '\0';
    CopyText(section, sectionSize, line + 1);
    Trim(section);
    return section[0] != '\0';
}

bool ParsePluginLine(const char *raw, char *path, size_t pathSize, bool *enabled)
{
    if (!raw || !path || !enabled) return false;
    char line[CONFIG_LINE_SIZE];
    CopyText(line, sizeof(line), raw);
    Trim(line);
    bool commented = false;
    if (line[0] == ';' || line[0] == '#')
    {
        commented = true;
        char *p = line + 1;
        while (*p && isspace((unsigned char)*p)) ++p;
        memmove(line, p, strlen(p) + 1);
    }
    if (line[0] != '/') return false;

    char *eq = strrchr(line, '=');
    *enabled = !commented;
    if (eq)
    {
        char state[32];
        CopyText(state, sizeof(state), eq + 1);
        Trim(state);
        if (strcmp(state, "true") == 0) *enabled = !commented;
        else if (strcmp(state, "false") == 0) *enabled = false;
        else return false;
        *eq = '\0';
        Trim(line);
    }
    if (!EndsWithPluginExtension(line)) return false;
    CopyText(path, pathSize, line);
    return path[0] != '\0';
}

void ShortenText(const char *src, char *dst, size_t dstSize, size_t maxChars)
{
    if (!dst || dstSize == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len <= maxChars || maxChars < 4)
    {
        CopyText(dst, dstSize, src);
        return;
    }
    size_t keep = maxChars - 3;
    if (keep >= dstSize) keep = dstSize - 1;
    memcpy(dst, src, keep);
    CopyText(dst + keep, dstSize - keep, "...");
}

void ShortenPath(const char *src, char *dst, size_t dstSize, size_t maxChars)
{
    if (!dst || dstSize == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t len = strlen(src);
    if (len <= maxChars) { CopyText(dst, dstSize, src); return; }
    const char *tail = src + len - (maxChars - 3);
    snprintf(dst, dstSize, "...%s", tail);
}
