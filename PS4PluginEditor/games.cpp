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
bool ReadSfoString(const char *path, const char *wantedKey, char *value, size_t valueSize)
{
    if (!path || !wantedKey || !value || valueSize == 0) return false;
    value[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long fileSize = ftell(f);
    if (fileSize < 20 || fileSize > 1024 * 1024 || fseek(f, 0, SEEK_SET) != 0)
    { fclose(f); return false; }
    unsigned char *data = (unsigned char *)malloc((size_t)fileSize);
    if (!data) { fclose(f); return false; }
    size_t got = fread(data, 1, (size_t)fileSize, f);
    fclose(f);
    bool found = false;
    if (got == (size_t)fileSize && data[0] == 0 && data[1] == 'P' && data[2] == 'S' && data[3] == 'F')
    {
        uint32_t keyTable = ReadLE32(data + 8);
        uint32_t dataTable = ReadLE32(data + 12);
        uint32_t count = ReadLE32(data + 16);
        if (count <= 512 && 20u + count * 16u <= got && keyTable < got && dataTable < got)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const unsigned char *entry = data + 20 + i * 16;
                uint16_t keyOff = ReadLE16(entry);
                uint32_t valueLen = ReadLE32(entry + 4);
                uint32_t valueOff = ReadLE32(entry + 12);
                size_t keyPos = (size_t)keyTable + keyOff;
                size_t valuePos = (size_t)dataTable + valueOff;
                if (keyPos >= got || valuePos >= got || valueLen == 0 || valueLen > 4096 || valuePos + valueLen > got)
                    continue;
                if (!memchr(data + keyPos, '\0', got - keyPos))
                    continue;
                if (strcmp((const char *)(data + keyPos), wantedKey) == 0)
                {
                    size_t n = valueLen;
                    // SFO strings are NUL-terminated/padded. Never copy bytes after
                    // the first NUL into a path or a UI label.
                    const unsigned char *nul = (const unsigned char *)memchr(data + valuePos, '\0', n);
                    if (nul) n = (size_t)(nul - (data + valuePos));
                    if (n >= valueSize) n = valueSize - 1;
                    memcpy(value, data + valuePos, n);
                    value[n] = '\0';
                    Trim(value);
                    found = value[0] != '\0';
                    break;
                }
            }
        }
    }
    free(data);
    return found;
}

bool ReadSfoTitle(const char *path, char *title, size_t titleSize)
{
    return ReadSfoString(path, "TITLE", title, titleSize);
}

bool ReadFallbackXmlTitle(const char *path, char *title, size_t titleSize)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buffer[32768];
    size_t n = fread(buffer, 1, sizeof(buffer) - 1, f);
    fclose(f);
    buffer[n] = '\0';
    char *start = strstr(buffer, "<text>");
    if (!start) return false;
    start += 6;
    char *end = strstr(start, "</text>");
    if (!end || end <= start) return false;
    size_t len = (size_t)(end - start);
    if (len >= titleSize) len = titleSize - 1;
    memcpy(title, start, len);
    title[len] = '\0';
    Trim(title);
    return title[0] != '\0';
}

bool IsPngFile(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && strcasecmp(dot, ".png") == 0 && FileExists(path);
}

bool FindPngRecursive(const char *root, char *foundPath, size_t foundSize,
                             int depth, bool preferIcon0)
{
    if (!root || depth > 6) return false;
    DIR *dir = opendir(root);
    if (!dir) return false;

    char fallback[512] = "";
    struct dirent *item;
    while ((item = readdir(dir)) != NULL)
    {
        if (!item->d_name[0] || item->d_name[0] == '.') continue;
        char full[512];
        int n = snprintf(full, sizeof(full), "%s/%s", root, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
        {
            char nested[512] = "";
            if (FindPngRecursive(full, nested, sizeof(nested), depth + 1, preferIcon0))
            {
                if (strrchr(nested, '/') && strcmp(strrchr(nested, '/') + 1, "icon0.png") == 0)
                {
                    CopyText(foundPath, foundSize, nested);
                    closedir(dir);
                    return true;
                }
                if (!fallback[0]) CopyText(fallback, sizeof(fallback), nested);
            }
        }
        else if (IsPngFile(full))
        {
            const char *base = strrchr(full, '/');
            base = base ? base + 1 : full;
            if (strcasecmp(base, "icon0.png") == 0)
            {
                CopyText(foundPath, foundSize, full);
                closedir(dir);
                return true;
            }
            if (!fallback[0]) CopyText(fallback, sizeof(fallback), full);
        }
    }
    closedir(dir);
    if (fallback[0])
    {
        CopyText(foundPath, foundSize, fallback);
        return true;
    }
    (void)preferIcon0;
    return false;
}

bool IsTitleIdFolderName(const char *name)
{
    // Match ItemzFlow's general title-ID filter: four letters followed by
    // five digits. This covers CUSA/PPSA as well as other valid title-ID
    // prefixes instead of hard-coding only two families.
    if (!name || strlen(name) != 9) return false;
    for (int i = 0; i < 4; ++i)
        if (!isalpha((unsigned char)name[i])) return false;
    for (int i = 4; i < 9; ++i)
        if (name[i] < '0' || name[i] > '9') return false;
    return true;
}

bool ResolveCurrentAppTitleId(char *titleId, size_t titleIdSize)
{
    if (!titleId || titleIdSize == 0) return false;
    titleId[0] = '\0';

    // Reading app0's own PARAM.SFO avoids hard-coding the title ID used to
    // install this tool (older builds used both PLUG00001 and PLUGN0001).
    char value[256] = "";
    if (ReadSfoString("/app0/sce_sys/param.sfo", "TITLE_ID", value, sizeof(value)) &&
        IsTitleIdFolderName(value))
    {
        CopyText(titleId, titleIdSize, value);
        return true;
    }
    if (!ReadSfoString("/app0/sce_sys/param.sfo", "CONTENT_ID", value, sizeof(value)))
        return false;

    // PS4 CONTENT_ID is commonly IV0000-TITLEID_00-... or UP0000-TITLEID_00-....
    const char *dash = strchr(value, '-');
    if (!dash || !dash[1]) return false;
    const char *id = dash + 1;
    const char *end = strchr(id, '_');
    size_t length = end ? (size_t)(end - id) : strlen(id);
    if (length != 9 || length >= titleIdSize) return false;
    char candidate[16] = "";
    memcpy(candidate, id, length);
    candidate[length] = '\0';
    if (!IsTitleIdFolderName(candidate)) return false;
    CopyText(titleId, titleIdSize, candidate);
    return true;
}

void SetGameMetadata(Game &game)
{
    // Keep the Title ID as a safe fallback, but prefer the actual game title
    // stored in param.sfo just as ItemzFlow does.
    CopyText(game.title, sizeof(game.title), game.id);
    const char *sfoRoots[] = {
        "/system_data/priv/appmeta",
        "/system_data/priv/appmeta/external",
        "/user/appmeta",
        "/user/appmeta/external"
    };
    for (size_t i = 0; i < sizeof(sfoRoots) / sizeof(sfoRoots[0]); ++i)
    {
        char sfoPath[512];
        int n = snprintf(sfoPath, sizeof(sfoPath), "%s/%s/param.sfo", sfoRoots[i], game.id);
        if (n > 0 && (size_t)n < sizeof(sfoPath) &&
            ReadSfoTitle(sfoPath, game.title, sizeof(game.title)))
            break;
    }

    // Match ItemzFlow's appmeta icon locations. Try the primary path first,
    // then the external appmeta directory when loading the page icons.
    snprintf(game.iconPath, sizeof(game.iconPath), "%s/%s/icon0.png", META_ROOT, game.id);
    snprintf(game.iconFallbackPath, sizeof(game.iconFallbackPath),
             "%s/external/%s/icon0.png", META_ROOT, game.id);
}

int CompareGames(const void *a, const void *b)
{
    const Game *ga = (const Game *)a;
    const Game *gb = (const Game *)b;
    return strcmp(ga->id, gb->id);
}

void FreePageIcons()
{
    for (int i = 0; i < GAME_PAGE_SIZE; ++i) FreePngImage(&gPageIcons[i]);
    gCachedPage = -1;
}

void LoadPageIcons(int pageStart)
{
    if (gCachedPage == pageStart) return;
    FreePageIcons();
    for (int slot = 0; slot < GAME_PAGE_SIZE; ++slot)
    {
        int index = pageStart + slot;
        if (index >= gGameCount) break;
        if (gGames[index].iconPath[0] &&
            !LoadPngRGBA(gGames[index].iconPath, &gPageIcons[slot]))
        {
            FreePngImage(&gPageIcons[slot]);
            if (gGames[index].iconFallbackPath[0])
                LoadPngRGBA(gGames[index].iconFallbackPath, &gPageIcons[slot]);
        }
    }
    gCachedPage = pageStart;
}

void EnsurePluginsIni()
{
    if (!DirectoryExists("/data/GoldHEN")) return;
    FILE *f = fopen(PLUGINS_INI, "a+");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0)
    {
        if (fprintf(f, "[default]\n") < 0 || fflush(f) != 0)
            snprintf(gStatus, sizeof(gStatus), "Could not initialize plugins.ini; it remains untouched where possible");
    }
    fclose(f);
}

void ScanInstalledAppsRoot(const char *root, int *entriesSeen, int *titleIdsSeen,
                                  int *rootsOpened, int *rootsFailed, int *lastErrno)
{
    if (!root || gGameCount >= MAX_GAMES) return;

    DIR *dir = opendir(root);
    if (!dir)
    {
        const int openError = errno;
        CopyText(gLastScanRoot, sizeof(gLastScanRoot), root);
        if (rootsFailed) ++(*rootsFailed);
        if (lastErrno) *lastErrno = openError;
        printf("RefreshGames: cannot open %s (errno=%d)\n", root, openError);
        return;
    }
    if (rootsOpened) ++(*rootsOpened);
    CopyText(gLastScanRoot, sizeof(gLastScanRoot), root);

    struct dirent *item;
    while ((item = readdir(dir)) != NULL && gGameCount < MAX_GAMES)
    {
        if (!item->d_name[0] || item->d_name[0] == '.') continue;
        if (entriesSeen) ++(*entriesSeen);
        if (!gFirstScanEntry[0])
        {
            CopyText(gFirstScanEntry, sizeof(gFirstScanEntry), item->d_name);
            CopyText(gFirstScanEntryRoot, sizeof(gFirstScanEntryRoot), root);
        }

        // Enumerate directories and validates the title ID after
        // enumeration. Confirm this entry is a directory before indexing it.
        if (!IsTitleIdFolderName(item->d_name)) continue;
        char entryPath[512];
        int pathLength = snprintf(entryPath, sizeof(entryPath), "%s/%s", root, item->d_name);
        if (pathLength <= 0 || (size_t)pathLength >= sizeof(entryPath)) continue;
        struct stat entryStat;
        if (stat(entryPath, &entryStat) != 0 || !S_ISDIR(entryStat.st_mode)) continue;
        if (titleIdsSeen) ++(*titleIdsSeen);

        bool duplicate = false;
        for (int i = 0; i < gGameCount; ++i)
        {
            if (strcasecmp(gGames[i].id, item->d_name) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        Game &game = gGames[gGameCount];
        memset(&game, 0, sizeof(game));
        CopyText(game.id, sizeof(game.id), item->d_name);
        SetGameMetadata(game); // icon0.png is resolved from /user/appmeta/<ID>/
        ++gGameCount;
    }

    closedir(dir);
}

void ScanTitleIdsRecursive(const char *root, int depth, int *entriesSeen,
                                  int *titleIdsSeen, int *dirsVisited)
{
    if (!root || depth < 0 || gGameCount >= MAX_GAMES ||
        (entriesSeen && *entriesSeen >= MAX_SCAN_ITEMS) ||
        (dirsVisited && *dirsVisited >= MAX_SCAN_ITEMS)) return;

    DIR *dir = opendir(root);
    if (!dir) return;
    if (dirsVisited) ++(*dirsVisited);

    struct dirent *item;
    while ((item = readdir(dir)) != NULL && gGameCount < MAX_GAMES)
    {
        if (!item->d_name[0] || item->d_name[0] == '.') continue;
        if (entriesSeen) {
            if (*entriesSeen >= MAX_SCAN_ITEMS) break;
            ++(*entriesSeen);
        }

        if (IsTitleIdFolderName(item->d_name))
        {
            if (titleIdsSeen) ++(*titleIdsSeen);
            bool duplicate = false;
            for (int i = 0; i < gGameCount; ++i)
                if (strcasecmp(gGames[i].id, item->d_name) == 0) {
                    duplicate = true;
                    break;
                }
            if (!duplicate)
            {
                Game &game = gGames[gGameCount];
                memset(&game, 0, sizeof(game));
                CopyText(game.id, sizeof(game.id), item->d_name);
                SetGameMetadata(game);
                ++gGameCount;
            }
            // Title-ID directories are the targets; don't recurse into them.
            continue;
        }

        if (depth == 0 || (dirsVisited && *dirsVisited >= MAX_SCAN_ITEMS)) continue;

        char full[512];
        int n = snprintf(full, sizeof(full), "%s/%s", root, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;

        struct stat st;
        if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        // Avoid known virtual or volatile trees if scanning from a broad root.
        if (strcmp(full, "/dev") == 0 || strcmp(full, "/proc") == 0 ||
            strcmp(full, "/sys") == 0) continue;
        ScanTitleIdsRecursive(full, depth - 1, entriesSeen, titleIdsSeen, dirsVisited);
    }
    closedir(dir);
}

void RefreshGames()
{
    FreePageIcons();
    gGameCount = 0;

    int entriesSeen = 0;
    int titleIdsSeen = 0;
    int rootsOpened = 0;
    int rootsFailed = 0;
    int lastErrno = 0;
    gLastScanRoot[0] = '\0';
    gFirstScanEntry[0] = '\0';
    gFirstScanEntryRoot[0] = '\0';
    int metadataErrnos[2] = {};

    // First resolve this app's actual Title ID from its own PARAM.SFO, then
    // mirror ItemzFlow's /user/app/<own-title-id>/../ trick to access /user/app.
    char currentTitleId[32] = "";
    char currentAppParentRoot[160] = "";
    if (ResolveCurrentAppTitleId(currentTitleId, sizeof(currentTitleId)))
        snprintf(currentAppParentRoot, sizeof(currentAppParentRoot),
                 "/user/app/%s/../", currentTitleId);

    const char *fallbackRoots[] = {
        "/user/app/PLUG00001/../",
        "/user/app/PLUGN0001/../",
        "/user/app/ITEM00001/../",
        "/user/app",
        "/mnt/ext0/user/app",
        "/mnt/ext1/user/app"
    };
    if (currentAppParentRoot[0])
    {
        ScanInstalledAppsRoot(currentAppParentRoot, &entriesSeen, &titleIdsSeen,
                              &rootsOpened, &rootsFailed, &lastErrno);
    }

    // Keep trying aliases: installed games can be split across internal and USB storage.
    for (size_t i = 0; i < sizeof(fallbackRoots) / sizeof(fallbackRoots[0]); ++i)
    {
        if (currentAppParentRoot[0] && strcmp(fallbackRoots[i], currentAppParentRoot) == 0)
            continue;
        ScanInstalledAppsRoot(fallbackRoots[i], &entriesSeen, &titleIdsSeen,
                              &rootsOpened, &rootsFailed, &lastErrno);
        if (gGameCount >= MAX_GAMES) break;
    }

    // App metadata is a fallback only. It is not the primary installed-app
    // index because stale metadata can remain after an app is removed.
    if (gGameCount == 0)
    {
        int openedBefore = rootsOpened;
        ScanInstalledAppsRoot("/system_data/priv/appmeta", &entriesSeen, &titleIdsSeen,
                              &rootsOpened, &rootsFailed, &lastErrno);
        metadataErrnos[0] = (rootsOpened > openedBefore) ? 0 : lastErrno;

        openedBefore = rootsOpened;
        ScanInstalledAppsRoot("/system_data/priv/appmeta/external", &entriesSeen, &titleIdsSeen,
                              &rootsOpened, &rootsFailed, &lastErrno);
        metadataErrnos[1] = (rootsOpened > openedBefore) ? 0 : lastErrno;
    }

    // If the standard ItemzFlow paths and metadata fallback are unavailable,
    // try bounded discovery from broader mount roots as a last resort.
    int fallbackDirsVisited = 0;
    if (gGameCount == 0)
    {
        const char *fallbackRoots[] = { "/mnt", "/user", "/system_data/priv" };
        for (size_t r = 0; r < sizeof(fallbackRoots) / sizeof(fallbackRoots[0]); ++r)
        {
            if (gGameCount >= MAX_GAMES || entriesSeen >= MAX_SCAN_ITEMS) break;
            ScanTitleIdsRecursive(fallbackRoots[r], 5, &entriesSeen,
                                  &titleIdsSeen, &fallbackDirsVisited);
        }
    }

    qsort(gGames, (size_t)gGameCount, sizeof(Game), CompareGames);

    if (gSelectedGame >= gGameCount)
        gSelectedGame = gGameCount > 0 ? gGameCount - 1 : 0;

    if (gGameCount > 0)
    {
        snprintf(gStatus, sizeof(gStatus),
                 "opened:%d entries:%d IDs:%d games:%d fallback dirs:%d",
                 rootsOpened, entriesSeen, titleIdsSeen, gGameCount, fallbackDirsVisited);
        LoadPageIcons((gSelectedGame / GAME_PAGE_SIZE) * GAME_PAGE_SIZE);
    }
    else if (rootsOpened == 0)
    {
        snprintf(gStatus, sizeof(gStatus),
                 "No roots opened; self:%s meta:%d/%d dirs:%d",
                 currentTitleId[0] ? currentTitleId : "unknown",
                 metadataErrnos[0], metadataErrnos[1], fallbackDirsVisited);
    }
    else if (titleIdsSeen == 0)
    {
        snprintf(gStatus, sizeof(gStatus),
                 "No IDs: roots:%d entries:%d first:%s @%.64s",
                 rootsOpened, entriesSeen,
                 gFirstScanEntry[0] ? gFirstScanEntry : "(none)",
                 gFirstScanEntryRoot[0] ? gFirstScanEntryRoot : gLastScanRoot);
    }
    else
    {
        snprintf(gStatus, sizeof(gStatus),
                 "scan roots:%d entries:%d title IDs:%d; game limit reached",
                 rootsOpened, entriesSeen, titleIdsSeen);
    }

    // Keep the reason for an inaccessible /user tree visible after refreshes.
    if (!gJbcActive)
    {
        char scanStatus[sizeof(gStatus)];
        CopyText(scanStatus, sizeof(scanStatus), gStatus);
        snprintf(gStatus, sizeof(gStatus), "JBC failed (%d); %.170s", gJbcError, scanStatus);
    }
}
