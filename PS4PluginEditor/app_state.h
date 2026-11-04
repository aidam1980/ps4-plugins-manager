#ifndef PS4PE_APP_STATE_H
#define PS4PE_APP_STATE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <sstream>
#include <graphics.h>
#include "png_decode.h"

#define SCREEN_W 1920
#define SCREEN_H 1080
#define MAX_GAMES 256
#define GAME_PAGE_SIZE 9
#define MAX_PLUGINS 256
#define MAX_CONFIG_ENTRIES 1024
#define MAX_CONFIG_LINES 1024
#define CONFIG_LINE_SIZE 768
#define MAX_SCAN_ITEMS 9000

struct Game { char id[64]; char title[160]; char iconPath[256]; char iconFallbackPath[256]; };
struct PluginEntry { char path[512]; bool enabled; bool configured; bool canToggle; };
struct ConfigEntry { char path[512]; char section[64]; bool enabled; };

extern std::stringstream debugLogStream;
extern const char *META_ROOT;
extern char gLastScanRoot[160];
extern char gFirstScanEntry[96];
extern char gFirstScanEntryRoot[160];
extern const char *PLUGIN_ROOT;
extern const char *PLUGINS_INI;
extern const char *TEMP_INI;
extern const Color BG, HEADER, CARD, CARD_ALT, ACCENT, WHITE, MUTED, GREEN, RED, AMBER, PANEL_BG, PANEL_EDGE, KEY_BG, KEY_EDGE, DIVIDER;
extern Game gGames[MAX_GAMES];
extern int gGameCount;
extern PngImage gPageIcons[GAME_PAGE_SIZE];
extern int gCachedPage;
extern PluginEntry gPlugins[MAX_PLUGINS];
extern int gPluginCount;
extern ConfigEntry gConfig[MAX_CONFIG_ENTRIES];
extern int gConfigCount;
extern char gConfigLines[MAX_CONFIG_LINES][CONFIG_LINE_SIZE];
extern int gConfigLineCount;
extern int gSelectedGame, gSelectedPlugin, gPluginScroll;
extern bool gInDetails, gShowSelectedPlugins;
extern PngImage gButtonCross, gButtonSquare, gButtonTriangle, gButtonL1, gButtonR1;
extern bool gButtonCrossAttempted, gButtonTriangleAttempted, gButtonL1Attempted, gButtonR1Attempted;
extern char gStatus[224];
extern bool gJbcActive;
extern int gJbcError;
extern int gIniCursor, gIniScroll;
extern bool gIniKeyboard, gIniBackupView;
extern char gIniEditBuffer[CONFIG_LINE_SIZE], gIniClipboard[CONFIG_LINE_SIZE];
extern bool gIniClipboardValid;
extern int gIniKeyboardCursor, gIniBackupCursor, gIniBackupScroll, gIniBackupCount;
extern char gIniBackupPaths[32][256];
extern char gIniBackupLabels[32][96];
extern const char *INI_KEYBOARD_CHARS;
extern int gTab, gBulkView, gBulkCount;
extern bool gPluginGridView;
extern bool gBulkSelected[MAX_GAMES];
extern int gBulkSelectedCount;

void CopyText(char *dst, size_t size, const char *src);
void Trim(char *text);
uint16_t ReadLE16(const unsigned char *p);
uint32_t ReadLE32(const unsigned char *p);
bool FileExists(const char *path);
bool DirectoryExists(const char *path);
bool EndsWithPluginExtension(const char *path);
bool IsSectionLine(const char *raw, char *section, size_t sectionSize);
bool ParsePluginLine(const char *raw, char *path, size_t pathSize, bool *enabled);
bool ReadSfoString(const char *path, const char *wantedKey, char *value, size_t valueSize);
bool ReadSfoTitle(const char *path, char *title, size_t titleSize);
bool ReadFallbackXmlTitle(const char *path, char *title, size_t titleSize);
bool IsPngFile(const char *path);
bool FindPngRecursive(const char *root, char *foundPath, size_t foundSize,
                             int depth, bool preferIcon0);
bool IsTitleIdFolderName(const char *name);
bool ResolveCurrentAppTitleId(char *titleId, size_t titleIdSize);
void SetGameMetadata(Game &game);
int CompareGames(const void *a, const void *b);
void FreePageIcons();
void LoadPageIcons(int pageStart);
void EnsurePluginsIni();
void ScanInstalledAppsRoot(const char *root, int *entriesSeen, int *titleIdsSeen,
                                  int *rootsOpened, int *rootsFailed, int *lastErrno);
void ScanTitleIdsRecursive(const char *root, int depth, int *entriesSeen,
                                  int *titleIdsSeen, int *dirsVisited);
void RefreshGames();
bool ParseSectionName(const char *raw, char *section, size_t sectionSize);
void LoadConfigEntries();
bool IsDefaultSection(const char *section);
bool EffectivePluginState(const char *path, const char *gameId, bool *configured);
int FindPlugin(const char *path);
void AddPluginPath(const char *path, const char *gameId);
void ScanPluginFilesRecursive(const char *root, const char *gameId, int depth, int *visited);
void LoadPluginsForGame(const Game &game);
bool CopyFileContents(const char *source, const char *target);
bool CreateBackupIfNeeded();
bool LoadConfigLines();
bool CreateTimestampedBackup(char *createdPath = NULL, size_t createdPathSize = 0);
bool SaveConfigLinesSafely(bool makeBackup);
bool EditSelectedIniLine(int32_t userID);
bool CommitIniEdit();
bool DeleteSelectedIniLine();
void RefreshIniBackups();
bool RestoreSelectedIniBackup();
bool DeleteSelectedIniBackup();
bool TogglePluginConfig(const Game &game, const PluginEntry &plugin);
void ShortenText(const char *src, char *dst, size_t dstSize, size_t maxChars);
void ShortenPath(const char *src, char *dst, size_t dstSize, size_t maxChars);
void DrawImage(Scene2D *scene, const PngImage &image, int x, int y, int w, int h, Color background);
void DrawTopBar(Scene2D *scene, FT_Face largeFont, FT_Face smallFont, const char *title, const char *subtitle);
void DrawPanel(Scene2D *scene, int x, int y, int w, int h, Color fill);
void DrawFooterKey(Scene2D *scene, FT_Face smallFont, const char *label,
                          int x, int y, int w, int h);
void DrawFooter(Scene2D *scene, FT_Face smallFont, const char *controls);
void LoadPluginCatalog();
void SelectTab(int nextTab);
bool LoadButtonAsset(const char *name, PngImage *image);
void DrawMyGamesFooter(Scene2D *scene, FT_Face font);
void DrawNavFooter(Scene2D *scene, FT_Face font, const char *controls);
void DrawAppChrome(Scene2D *scene, FT_Face largeFont, FT_Face smallFont,
                          const char *subtitle);
void DrawGameGrid(Scene2D *scene, FT_Face smallFont, bool multiSelect);
void DrawMyGames(Scene2D *scene, FT_Face largeFont, FT_Face smallFont);
void DrawGamePlugins(Scene2D *scene, FT_Face largeFont, FT_Face smallFont);
void DrawPluginsIni(Scene2D *scene, FT_Face largeFont, FT_Face smallFont);
void DrawIniKeyboard(Scene2D *scene, FT_Face largeFont, FT_Face smallFont);
void DrawIniBackups(Scene2D *scene, FT_Face largeFont, FT_Face smallFont);
void ApplyPluginToSelectedGames();

#endif
