#include <ctype.h>
#include "app_state.h"

std::stringstream debugLogStream;
const char *META_ROOT = "/user/appmeta";
char gLastScanRoot[160] = "(not tried)";
char gFirstScanEntry[96] = "";
char gFirstScanEntryRoot[160] = "";
const char *PLUGIN_ROOT = "/data/GoldHEN/plugins";
const char *PLUGINS_INI = "/data/GoldHEN/plugins.ini";
const char *TEMP_INI = "/data/GoldHEN/plugins.ini.tmp";
const Color BG = {15,19,27}, HEADER = {24,31,43}, CARD = {28,36,49}, CARD_ALT = {38,52,72}, ACCENT = {82,176,236}, WHITE = {236,241,248}, MUTED = {150,164,181}, GREEN = {108,204,152}, RED = {226,116,120}, AMBER = {226,184,108}, PANEL_BG = {22,28,39}, PANEL_EDGE = {46,58,76}, KEY_BG = {30,39,51}, KEY_EDGE = {82,176,236}, DIVIDER = {50,62,80};
// Extra shades for the refreshed UI: soft shadows, selection glow and tinted badge backgrounds.
const Color SHADOW_SOFT = {11,14,20}, SHADOW_DEEP = {8,10,15}, ACCENT_GLOW = {30,62,86}, ACCENT_DIM = {28,56,78}, ROW_ALT = {26,33,45}, NEUTRAL_DIM = {40,50,66}, GREEN_DIM = {24,60,48}, RED_DIM = {68,34,40}, AMBER_DIM = {68,56,30};
Game gGames[MAX_GAMES] = {}; int gGameCount = 0; PngImage gPageIcons[GAME_PAGE_SIZE] = {}; int gCachedPage = -1;
PluginEntry gPlugins[MAX_PLUGINS] = {}; int gPluginCount = 0; ConfigEntry gConfig[MAX_CONFIG_ENTRIES] = {}; int gConfigCount = 0;
char gConfigLines[MAX_CONFIG_LINES][CONFIG_LINE_SIZE] = {}; int gConfigLineCount = 0;
int gSelectedGame = 0, gSelectedPlugin = 0, gPluginScroll = 0; bool gInDetails = false, gShowSelectedPlugins = false;
PngImage gButtonCross = {}, gButtonSquare = {}, gButtonTriangle = {}, gButtonL1 = {}, gButtonR1 = {};
bool gButtonCrossAttempted = false, gButtonTriangleAttempted = false, gButtonL1Attempted = false, gButtonR1Attempted = false;
char gStatus[224] = "Scanning installed games..."; bool gJbcActive = false; int gJbcError = 0;
int gIniCursor = 0, gIniScroll = 0; bool gIniKeyboard = false, gIniBackupView = false;
char gIniEditBuffer[CONFIG_LINE_SIZE] = {}, gIniClipboard[CONFIG_LINE_SIZE] = {}; bool gIniClipboardValid = false;
int gIniKeyboardCursor = 0, gIniBackupCursor = 0, gIniBackupScroll = 0, gIniBackupCount = 0;
char gIniBackupPaths[32][256] = {}, gIniBackupLabels[32][96] = {};
const char *INI_KEYBOARD_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 /._-\\[](){}=+#;:,!?@%&*'\"";
int gTab = 0, gBulkView = 0, gBulkCount = 1;
bool gPluginGridView = false;
bool gBulkSelected[MAX_GAMES] = {}; int gBulkSelectedCount = 0;

int gGetView = 0, gGetMenuCursor = 0, gUsbCursor = 0, gUsbScroll = 0, gUsbCount = 0;
GetUsbEntry gUsbEntries[MAX_GET_USB_ITEMS] = {};
GetOnlineEntry gOnlineEntries[MAX_GET_ONLINE_ITEMS] = {};
int gOnlineCount = 0, gOnlineCursor = 0, gOnlineScroll = 0, gOnlinePendingIndex = -1;
bool gOnlineConfirmOverwrite = false;
bool gGetConfirmImport = false, gGetConfirmDelete = false;

GetBrowserEntry gBrowserEntries[MAX_GET_USB_ITEMS] = {};
int gBrowserCount = 0, gBrowserCursor = 0, gBrowserScroll = 0;
char gBrowserPath[512] = "/";
bool gBrowserConfirmDelete = false, gBrowserFolderOptions = false;
int gBrowserFolderOptionCursor = 0;
char gGetShortcuts[32][512] = {};
int gGetShortcutCount = 0;
bool gBrowserFolderImportPending = false;
bool gBrowserShortcutMode = false, gGetConfirmShortcutDelete = false;

// ---------------------------------------------------------------------------
// My Games search
// ---------------------------------------------------------------------------
char gSearchQuery[48] = {};
int gFilteredGames[MAX_GAMES] = {}, gFilteredCount = 0;
bool gNavFast = false;

static bool ContainsNoCase(const char *hay, const char *needle, size_t needleLen)
{
    if (needleLen == 0) return true;
    for (const char *h = hay; *h; ++h)
    {
        size_t i = 0;
        while (i < needleLen && h[i] &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == needleLen) return true;
    }
    return false;
}

// Every space-separated word must appear in the game title or title ID.
static bool GameMatches(const Game &game, const char *query)
{
    const char *p = query;
    while (*p)
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ') ++p;
        const size_t len = (size_t)(p - start);
        if (!ContainsNoCase(game.title, start, len) && !ContainsNoCase(game.id, start, len))
            return false;
    }
    return true;
}

void RebuildGameFilter()
{
    gFilteredCount = 0;
    for (int i = 0; i < gGameCount && gFilteredCount < MAX_GAMES; ++i)
        if (gSearchQuery[0] == '\0' || GameMatches(gGames[i], gSearchQuery))
            gFilteredGames[gFilteredCount++] = i;
}

int GameFilterPosition(int gameIndex)
{
    for (int i = 0; i < gFilteredCount; ++i)
        if (gFilteredGames[i] == gameIndex) return i;
    return -1;
}
