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
bool ParseSectionName(const char *raw, char *section, size_t sectionSize)
{
    return IsSectionLine(raw, section, sectionSize);
}

void LoadConfigEntries()
{
    gConfigCount = 0;
    FILE *f = fopen(PLUGINS_INI, "r");
    if (!f)
    {
        snprintf(gStatus, sizeof(gStatus), "plugins.ini not found; it will be created on first toggle");
        return;
    }
    char line[CONFIG_LINE_SIZE];
    char currentSection[64] = "default";
    while (fgets(line, sizeof(line), f))
    {
        char section[64];
        if (ParseSectionName(line, section, sizeof(section)))
        {
            CopyText(currentSection, sizeof(currentSection), section);
            continue;
        }
        char pluginPath[512];
        bool enabled = false;
        if (!ParsePluginLine(line, pluginPath, sizeof(pluginPath), &enabled)) continue;
        if (gConfigCount >= MAX_CONFIG_ENTRIES) break;
        ConfigEntry &entry = gConfig[gConfigCount++];
        CopyText(entry.path, sizeof(entry.path), pluginPath);
        CopyText(entry.section, sizeof(entry.section), currentSection);
        entry.enabled = enabled;
    }
    fclose(f);
}

bool IsDefaultSection(const char *section)
{
    return section && (strcasecmp(section, "default") == 0 || section[0] == '\0');
}

bool EffectivePluginState(const char *path, const char *gameId, bool *configured)
{
    bool haveDefault = false, defaultState = false;
    bool haveGame = false, gameState = false;
    for (int i = 0; i < gConfigCount; ++i)
    {
        if (strcmp(gConfig[i].path, path) != 0) continue;
        if (IsDefaultSection(gConfig[i].section))
        {
            haveDefault = true;
            defaultState = gConfig[i].enabled;
        }
        if (gameId && strcasecmp(gConfig[i].section, gameId) == 0)
        {
            haveGame = true;
            gameState = gConfig[i].enabled;
        }
    }
    if (configured) *configured = haveDefault || haveGame;
    return haveGame ? gameState : (haveDefault ? defaultState : false);
}

int FindPlugin(const char *path)
{
    for (int i = 0; i < gPluginCount; ++i)
        if (strcmp(gPlugins[i].path, path) == 0) return i;
    return -1;
}

void AddPluginPath(const char *path, const char *gameId)
{
    if (!path || !EndsWithPluginExtension(path)) return;
    int found = FindPlugin(path);
    bool configured = false;
    bool enabled = EffectivePluginState(path, gameId, &configured);
    bool isInGoldHENPluginRoot = strncmp(path, "/data/GoldHEN/plugins/", 22) == 0;
    if (found >= 0)
    {
        if (configured)
        {
            gPlugins[found].configured = true;
            gPlugins[found].enabled = enabled;
            gPlugins[found].canToggle = true;
        }
        if (isInGoldHENPluginRoot) gPlugins[found].canToggle = true;
        return;
    }
    if (gPluginCount >= MAX_PLUGINS) return;
    PluginEntry &p = gPlugins[gPluginCount++];
    CopyText(p.path, sizeof(p.path), path);
    p.enabled = enabled;
    p.configured = configured;
    // Don't accidentally register arbitrary game-owned SPRX modules as GoldHEN plugins.
    p.canToggle = configured || isInGoldHENPluginRoot;
}

void ScanPluginFilesRecursive(const char *root, const char *gameId, int depth, int *visited)
{
    if (!root || depth > 5 || *visited >= MAX_SCAN_ITEMS || gPluginCount >= MAX_PLUGINS)
        return;
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL && *visited < MAX_SCAN_ITEMS && gPluginCount < MAX_PLUGINS)
    {
        if (!item->d_name[0] || item->d_name[0] == '.') continue;
        ++(*visited);
        char full[512];
        int n = snprintf(full, sizeof(full), "%s/%s", root, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            ScanPluginFilesRecursive(full, gameId, depth + 1, visited);
        else if (EndsWithPluginExtension(full))
            AddPluginPath(full, gameId);
    }
    closedir(dir);
}

void LoadPluginsForGame(const Game &game)
{
    gPluginCount = 0;
    gSelectedPlugin = 0;
    gPluginScroll = 0;
    LoadConfigEntries();
    // My Games intentionally lists files from the GoldHEN plugins directory,
    // with the selected game's [TITLE_ID] section used to show its own state.
    int visited = 0;
    ScanPluginFilesRecursive(PLUGIN_ROOT, game.id, 0, &visited);
    // Keep configured paths visible even when a plugin file has been removed,
    // so plugins.ini can be inspected and stale entries can be spotted.
    for (int i = 0; i < gConfigCount && gPluginCount < MAX_PLUGINS; ++i)
    {
        if (!IsDefaultSection(gConfig[i].section) && strcasecmp(gConfig[i].section, game.id) != 0)
            continue;
        AddPluginPath(gConfig[i].path, game.id);
    }
    if (gPluginCount > 0)
        snprintf(gStatus, sizeof(gStatus), "%d plugin files / entries; X toggles only %s", gPluginCount, game.id);
    else
        snprintf(gStatus, sizeof(gStatus), "No PRX/SPRX files in /data/GoldHEN/plugins");
}

bool TogglePluginConfig(const Game &game, const PluginEntry &plugin)
{
    if (!plugin.canToggle)
    {
        snprintf(gStatus, sizeof(gStatus), "Read-only game module; only GoldHEN plugin files or configured paths can be toggled");
        return false;
    }
    if (!DirectoryExists("/data/GoldHEN"))
    {
        snprintf(gStatus, sizeof(gStatus), "Missing /data/GoldHEN; plugins.ini not changed");
        return false;
    }
    if (!LoadConfigLines()) return false;

    bool newEnabled = !plugin.enabled;
    bool sectionFound = false;
    int lastSectionStart = -1;
    int lastSectionEnd = gConfigLineCount;
    int matchLine = -1;
    char currentSection[64] = "default";

    for (int i = 0; i < gConfigLineCount; ++i)
    {
        char section[64];
        if (ParseSectionName(gConfigLines[i], section, sizeof(section)))
        {
            if (sectionFound && lastSectionEnd == gConfigLineCount)
                lastSectionEnd = i;
            CopyText(currentSection, sizeof(currentSection), section);
            if (strcasecmp(section, game.id) == 0)
            {
                sectionFound = true;
                lastSectionStart = i;
                lastSectionEnd = gConfigLineCount;
            }
            continue;
        }
        if (strcasecmp(currentSection, game.id) == 0)
        {
            char existingPath[512];
            bool existingEnabled = false;
            if (ParsePluginLine(gConfigLines[i], existingPath, sizeof(existingPath), &existingEnabled) &&
                strcmp(existingPath, plugin.path) == 0)
                matchLine = i;
        }
    }
    if (sectionFound && lastSectionStart >= 0)
    {
        lastSectionEnd = gConfigLineCount;
        for (int i = lastSectionStart + 1; i < gConfigLineCount; ++i)
        {
            char section[64];
            if (ParseSectionName(gConfigLines[i], section, sizeof(section)))
            {
                lastSectionEnd = i;
                break;
            }
        }
    }

    if (!CreateBackupIfNeeded())
    {
        snprintf(gStatus, sizeof(gStatus), "Could not create plugins.ini backup; no change made");
        return false;
    }

    char newLine[CONFIG_LINE_SIZE];
    snprintf(newLine, sizeof(newLine), "%s=%s\n", plugin.path, newEnabled ? "true" : "false");
    if (matchLine >= 0)
    {
        CopyText(gConfigLines[matchLine], CONFIG_LINE_SIZE, newLine);
    }
    else
    {
        if (gConfigLineCount + 2 >= MAX_CONFIG_LINES)
        {
            snprintf(gStatus, sizeof(gStatus), "plugins.ini is full; no change made");
            return false;
        }
        if (sectionFound)
        {
            for (int i = gConfigLineCount; i > lastSectionEnd; --i)
                CopyText(gConfigLines[i], CONFIG_LINE_SIZE, gConfigLines[i - 1]);
            CopyText(gConfigLines[lastSectionEnd], CONFIG_LINE_SIZE, newLine);
            ++gConfigLineCount;
        }
        else
        {
            if (gConfigLineCount > 0 && gConfigLines[gConfigLineCount - 1][0] != '\0')
            {
                size_t lastLen = strlen(gConfigLines[gConfigLineCount - 1]);
                if (lastLen > 0 && gConfigLines[gConfigLineCount - 1][lastLen - 1] != '\n')
                {
                    if (lastLen + 1 < CONFIG_LINE_SIZE)
                    {
                        gConfigLines[gConfigLineCount - 1][lastLen] = '\n';
                        gConfigLines[gConfigLineCount - 1][lastLen + 1] = '\0';
                    }
                }
            }
            snprintf(gConfigLines[gConfigLineCount++], CONFIG_LINE_SIZE, "[%s]\n", game.id);
            CopyText(gConfigLines[gConfigLineCount++], CONFIG_LINE_SIZE, newLine);
        }
    }

    // A default-on assignment should truly apply globally. Remove older per-game
    // overrides for this same path so a stale false entry cannot silently disable it.
    if (strcasecmp(game.id, "default") == 0 && newEnabled)
    {
        char activeSection[64] = "default";
        for (int i = 0; i < gConfigLineCount; )
        {
            char section[64];
            if (ParseSectionName(gConfigLines[i], section, sizeof(section)))
            {
                CopyText(activeSection, sizeof(activeSection), section);
                ++i;
                continue;
            }
            char existingPath[512];
            bool existingEnabled = false;
            bool samePlugin = ParsePluginLine(gConfigLines[i], existingPath, sizeof(existingPath), &existingEnabled) &&
                              strcmp(existingPath, plugin.path) == 0;
            if (samePlugin && !IsDefaultSection(activeSection))
            {
                for (int j = i; j + 1 < gConfigLineCount; ++j)
                    CopyText(gConfigLines[j], CONFIG_LINE_SIZE, gConfigLines[j + 1]);
                --gConfigLineCount;
                continue;
            }
            ++i;
        }
    }

    // Rewrite via a temporary file so a failed write does not truncate the live file.
    FILE *out = fopen(TEMP_INI, "wb");
    if (!out)
    {
        snprintf(gStatus, sizeof(gStatus), "Cannot write temporary plugins.ini");
        return false;
    }
    bool ok = true;
    if (gConfigLineCount == 0)
    {
        if (fwrite("[default]\n", 1, 10, out) != 10) ok = false;
    }
    for (int i = 0; ok && i < gConfigLineCount; ++i)
    {
        size_t len = strlen(gConfigLines[i]);
        if (len && fwrite(gConfigLines[i], 1, len, out) != len) ok = false;
        if (i + 1 < gConfigLineCount && (!len || gConfigLines[i][len - 1] != '\n'))
            if (fputc('\n', out) == EOF) ok = false;
    }
    if (fclose(out) != 0) ok = false;
    if (!ok || rename(TEMP_INI, PLUGINS_INI) != 0)
    {
        remove(TEMP_INI);
        snprintf(gStatus, sizeof(gStatus), "Failed to save plugins.ini");
        return false;
    }
    snprintf(gStatus, sizeof(gStatus), "%s in plugins.ini; restart game if needed", newEnabled ? "Enabled" : "Disabled");
    return true;
}

void LoadPluginCatalog()
{
    gPluginCount = 0;
    gSelectedPlugin = 0;
    gPluginScroll = 0;
    LoadConfigEntries();
    int visited = 0;
    ScanPluginFilesRecursive(PLUGIN_ROOT, "", 0, &visited);
    if (gPluginCount > 0)
        snprintf(gStatus, sizeof(gStatus), "%d PRX/SPRX files found in %s", gPluginCount, PLUGIN_ROOT);
    else
        snprintf(gStatus, sizeof(gStatus), "No PRX/SPRX files found in /data/GoldHEN/plugins");
}

void SelectTab(int nextTab)
{
    if (nextTab < 0) nextTab = 2;
    if (nextTab > 2) nextTab = 0;
    gTab = nextTab;
    gInDetails = false;
    gIniKeyboard = false;
    gIniBackupView = false;
    gBulkView = 0;
    gBulkSelectedCount = 0;
    memset(gBulkSelected, 0, sizeof(gBulkSelected));
    if (gTab == 0)
    {
        // Installed games are indexed once during application startup.
        // Switching back to My Games reuses the in-memory catalog.
        // RefreshGames() remains available for an explicit user-triggered rescan.
    }
    else if (gTab == 1)
    {
        LoadPluginCatalog();
    }
    else
    {
        LoadConfigLines();
        LoadConfigEntries();
        gIniCursor = 0;
        gIniScroll = 0;
        if (gConfigLineCount == 0)
            snprintf(gStatus, sizeof(gStatus), "No readable plugins.ini at %s", PLUGINS_INI);
        else
            snprintf(gStatus, sizeof(gStatus), "%d lines loaded from plugins.ini", gConfigLineCount);
    }
}

void ApplyPluginToSelectedGames()
{
    if (gBulkSelectedCount == 0 || gSelectedPlugin < 0 || gSelectedPlugin >= gPluginCount)
    {
        snprintf(gStatus, sizeof(gStatus), "Select at least one game first");
        return;
    }
    char pluginPath[512];
    CopyText(pluginPath, sizeof(pluginPath), gPlugins[gSelectedPlugin].path);
    int successes = 0;
    int failures = 0;
    for (int i = 0; i < gGameCount; ++i)
    {
        if (!gBulkSelected[i]) continue;
        LoadConfigEntries();
        bool configured = false;
        bool enabled = EffectivePluginState(pluginPath, gGames[i].id, &configured);
        if (enabled) { ++successes; continue; }
        PluginEntry choice = {};
        CopyText(choice.path, sizeof(choice.path), pluginPath);
        choice.enabled = false;
        choice.configured = configured;
        choice.canToggle = true;
        if (TogglePluginConfig(gGames[i], choice)) ++successes;
        else ++failures;
    }
    char resultMessage[sizeof(gStatus)];
    snprintf(resultMessage, sizeof(resultMessage), "Applied to %d selected games; failures:%d", successes, failures);
    LoadPluginCatalog();
    CopyText(gStatus, sizeof(gStatus), resultMessage);
    gBulkView = 0;
    gBulkSelectedCount = 0;
    memset(gBulkSelected, 0, sizeof(gBulkSelected));
}
