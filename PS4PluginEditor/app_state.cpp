#include "app_state.h"

std::stringstream debugLogStream;
const char *META_ROOT = "/user/appmeta";
char gLastScanRoot[160] = "(not tried)";
char gFirstScanEntry[96] = "";
char gFirstScanEntryRoot[160] = "";
const char *PLUGIN_ROOT = "/data/GoldHEN/plugins";
const char *PLUGINS_INI = "/data/GoldHEN/plugins.ini";
const char *TEMP_INI = "/data/GoldHEN/plugins.ini.tmp";
const Color BG = {18,22,30}, HEADER = {25,31,41}, CARD = {29,36,47}, CARD_ALT = {38,49,63}, ACCENT = {75,166,214}, WHITE = {232,238,245}, MUTED = {157,170,185}, GREEN = {103,194,145}, RED = {220,112,116}, AMBER = {218,177,105}, PANEL_BG = {23,29,39}, PANEL_EDGE = {49,61,76}, KEY_BG = {30,39,51}, KEY_EDGE = {75,166,214}, DIVIDER = {54,66,82};
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
