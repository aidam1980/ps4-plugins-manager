#include <stdio.h>
#include <sstream>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <graphics.h>
#include "png_decode.h"
#include "app_state.h"

// Implemented in jbc_bootstrap.cpp; runs before filesystem discovery.
extern "C" int GoldenPluginE_JbcBootstrap(void);


// Application entry point and controller/event loop.


int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("Plugin Manager starting\n");

    // Run libjbc before touching /user/app, appmeta, or /data/GoldHEN.
    // The bootstrap returns stage-specific negative values on failure and
    // intentionally lets the UI continue so diagnostics remain available.
    gJbcError = GoldenPluginE_JbcBootstrap();
    gJbcActive = (gJbcError == 0);
    if (gJbcActive)
        printf("JBC: sandbox credentials updated\n");
    else
        printf("WARNING: JBC bootstrap failed (stage=%d); continuing without confirmed access\n", gJbcError);

    printf("Plugin root: %s\nplugins.ini: %s\n", PLUGIN_ROOT, PLUGINS_INI);

    Scene2D *scene = new Scene2D(SCREEN_W, SCREEN_H, 4);
    if (!scene)
    {
        printf("Failed to create Scene2D\n");
        for (;;) sceKernelUsleep(1000000);
    }
    if (!scene->Init(0xC000000, 2))
    {
        printf("Graphics initialization failed\n");
        for (;;) sceKernelUsleep(1000000);
    }

    const char *fontPath = "/app0/assets/fonts/Gontserrat-Regular.ttf";
    FT_Face largeFont = nullptr;
    FT_Face smallFont = nullptr;
    if (!scene->InitFont(&largeFont, fontPath, 42) || !scene->InitFont(&smallFont, fontPath, 27))
    {
        printf("Font initialization failed: %s\n", fontPath);
        for (;;) sceKernelUsleep(1000000);
    }

    int padHandle = -1;
    int32_t userID = -1;
    if (scePadInit() == 0)
    {
        OrbisUserServiceInitializeParams params = {};
        params.priority = ORBIS_KERNEL_PRIO_FIFO_LOWEST;
        if (sceUserServiceInitialize(&params) == 0 && sceUserServiceGetInitialUser(&userID) == 0)
            padHandle = scePadOpen(userID, 0, 0, NULL);
    }

    EnsurePluginsIni();
    RefreshGames();
    uint32_t previousButtons = 0;
    uint32_t previousAnalogDirections = 0;
    int frameID = 0;
    for (;;)
    {
        uint32_t pressed = 0;
        if (padHandle >= 0)
        {
            OrbisPadData pad = {};
            if (scePadReadState(padHandle, &pad) >= 0 && pad.connected)
            {
                pressed = pad.buttons & ~previousButtons;
                previousButtons = pad.buttons;

                // Left analog stick behaves exactly like the D-pad.
                // A 32-point dead zone around neutral (128) avoids stick drift.
                uint32_t analogDirections = 0;
                const int stickX = (int)pad.leftStick.x;
                const int stickY = (int)pad.leftStick.y;
                if (stickX < 96)  analogDirections |= ORBIS_PAD_BUTTON_LEFT;
                if (stickX > 160) analogDirections |= ORBIS_PAD_BUTTON_RIGHT;
                if (stickY < 96)  analogDirections |= ORBIS_PAD_BUTTON_UP;
                if (stickY > 160) analogDirections |= ORBIS_PAD_BUTTON_DOWN;

                // Fire once when entering a direction, just like a D-pad press.
                pressed |= analogDirections & ~previousAnalogDirections;
                previousAnalogDirections = analogDirections;
            }
            else
            {
                previousButtons = 0;
                previousAnalogDirections = 0;
            }
        }

        // L1/R1 change only among the three requested top-level lists.
        if (pressed & ORBIS_PAD_BUTTON_L1) SelectTab(gTab - 1);
        if (pressed & ORBIS_PAD_BUTTON_R1) SelectTab(gTab + 1);

        if (gTab == 0)
        {
            if (!gInDetails)
            {
                int oldPage = gSelectedGame / GAME_PAGE_SIZE;
                if (gGameCount > 0)
                {
                    if (pressed & ORBIS_PAD_BUTTON_LEFT)  gSelectedGame = (gSelectedGame + gGameCount - 1) % gGameCount;
                    if (pressed & ORBIS_PAD_BUTTON_RIGHT) gSelectedGame = (gSelectedGame + 1) % gGameCount;
                    if ((pressed & ORBIS_PAD_BUTTON_UP) && gSelectedGame >= 3) gSelectedGame -= 3;
                    if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gSelectedGame + 3 < gGameCount) gSelectedGame += 3;
                    if ((gSelectedGame / GAME_PAGE_SIZE) != oldPage)
                        LoadPageIcons((gSelectedGame / GAME_PAGE_SIZE) * GAME_PAGE_SIZE);
                    if (pressed & ORBIS_PAD_BUTTON_CROSS)
                    {
                        gShowSelectedPlugins = false;
                        gInDetails = true;
                        LoadPluginsForGame(gGames[gSelectedGame]);
                    }
                    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE) gShowSelectedPlugins = !gShowSelectedPlugins;
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) gShowSelectedPlugins = false;
                DrawMyGames(scene, largeFont, smallFont);
            }
            else
            {
                if (gPluginCount > 0)
                {
                    if ((pressed & ORBIS_PAD_BUTTON_UP) && gSelectedPlugin > 0) --gSelectedPlugin;
                    if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gSelectedPlugin + 1 < gPluginCount) ++gSelectedPlugin;
                    if (pressed & ORBIS_PAD_BUTTON_CROSS)
                    {
                        PluginEntry chosen = gPlugins[gSelectedPlugin];
                        TogglePluginConfig(gGames[gSelectedGame], chosen);
                        char notice[sizeof(gStatus)];
                        CopyText(notice, sizeof(notice), gStatus);
                        LoadPluginsForGame(gGames[gSelectedGame]);
                        CopyText(gStatus, sizeof(gStatus), notice);
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { gInDetails = false; gStatus[0] = '\0'; }
                if (pressed & ORBIS_PAD_BUTTON_SQUARE) LoadPluginsForGame(gGames[gSelectedGame]);
                DrawMyGames(scene, largeFont, smallFont);
            }
        }
        else if (gTab == 1)
        {
            if (gBulkView == 0)
            {
                if (pressed & ORBIS_PAD_BUTTON_R3) gPluginGridView = !gPluginGridView;
                if (gPluginCount > 0)
                {
                    if (gPluginGridView)
                    {
                        // Three-column grid navigation: vertical arrows move by a row,
                        // horizontal arrows stay within the current row.
                        int col = gSelectedPlugin % 3;
                        if ((pressed & ORBIS_PAD_BUTTON_LEFT) && col > 0) --gSelectedPlugin;
                        if ((pressed & ORBIS_PAD_BUTTON_RIGHT) && col < 2 && gSelectedPlugin + 1 < gPluginCount) ++gSelectedPlugin;
                        if ((pressed & ORBIS_PAD_BUTTON_UP) && gSelectedPlugin >= 3) gSelectedPlugin -= 3;
                        else if ((pressed & ORBIS_PAD_BUTTON_UP) && gSelectedPlugin > 0) --gSelectedPlugin;
                        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gSelectedPlugin + 3 < gPluginCount) gSelectedPlugin += 3;
                        else if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gSelectedPlugin + 1 < gPluginCount) ++gSelectedPlugin;
                    }
                    else
                    {
                        if ((pressed & ORBIS_PAD_BUTTON_UP) && gSelectedPlugin > 0) --gSelectedPlugin;
                        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gSelectedPlugin + 1 < gPluginCount) ++gSelectedPlugin;
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CROSS)
                {
                    if (gGameCount <= 0)
                        snprintf(gStatus, sizeof(gStatus), "No games found; cannot assign plugin yet");
                    else if (gPluginCount > 0)
                    {
                        // Skip the count prompt. Let the user select any number of games.
                        memset(gBulkSelected, 0, sizeof(gBulkSelected));
                        gBulkSelectedCount = 0;
                        // Seed the game list with the plugin's current effective state.
                        LoadConfigEntries();
                        const char *selectedPath = gPlugins[gSelectedPlugin].path;
                        for (int gi = 0; gi < gGameCount; ++gi)
                        {
                            bool isEnabled = EffectivePluginState(selectedPath, gGames[gi].id, NULL);
                            gBulkSelected[gi] = isEnabled;
                            if (isEnabled) ++gBulkSelectedCount;
                        }
                        gBulkView = 2;
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_OPTIONS)
                {
                    if (gPluginCount > 0)
                    {
                        Game defaultSection = {};
                        CopyText(defaultSection.id, sizeof(defaultSection.id), "default");
                        LoadConfigEntries();
                        PluginEntry choice = gPlugins[gSelectedPlugin];
                        char selectedPath[sizeof(choice.path)];
                        CopyText(selectedPath, sizeof(selectedPath), choice.path);
                        choice.enabled = EffectivePluginState(choice.path, "", NULL);
                        bool ok = TogglePluginConfig(defaultSection, choice);
                        char notice[sizeof(gStatus)];
                        CopyText(notice, sizeof(notice), gStatus);
                        LoadPluginCatalog();
                        int restored = FindPlugin(selectedPath);
                        if (restored >= 0) gSelectedPlugin = restored;
                        CopyText(gStatus, sizeof(gStatus), ok ? "Default state updated for ALL apps" : notice);
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_SQUARE) LoadPluginCatalog();
            }
            else if (gBulkView == 1)
            {
                if ((pressed & ORBIS_PAD_BUTTON_UP) && gBulkCount < gGameCount) ++gBulkCount;
                if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gBulkCount > 1) --gBulkCount;
                if (pressed & ORBIS_PAD_BUTTON_CROSS)
                {
                    memset(gBulkSelected, 0, sizeof(gBulkSelected));
                    gBulkSelectedCount = 0;
                    gSelectedGame = 0;
                    gBulkView = 2;
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) gBulkView = 0;
            }
            else
            {
                int oldPage = gSelectedGame / GAME_PAGE_SIZE;
                if (gGameCount > 0)
                {
                    if (pressed & ORBIS_PAD_BUTTON_LEFT)  gSelectedGame = (gSelectedGame + gGameCount - 1) % gGameCount;
                    if (pressed & ORBIS_PAD_BUTTON_RIGHT) gSelectedGame = (gSelectedGame + 1) % gGameCount;
                    if ((pressed & ORBIS_PAD_BUTTON_UP) && gSelectedGame >= 3) gSelectedGame -= 3;
                    if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gSelectedGame + 3 < gGameCount) gSelectedGame += 3;
                    if ((gSelectedGame / GAME_PAGE_SIZE) != oldPage)
                        LoadPageIcons((gSelectedGame / GAME_PAGE_SIZE) * GAME_PAGE_SIZE);
                    if (pressed & ORBIS_PAD_BUTTON_CROSS)
                    {
                        // Apply immediately to this game; no Triangle confirmation step.
                        LoadConfigEntries();
                        PluginEntry choice = {};
                        CopyText(choice.path, sizeof(choice.path), gPlugins[gSelectedPlugin].path);
                        choice.enabled = EffectivePluginState(choice.path, gGames[gSelectedGame].id, NULL);
                        choice.configured = true;
                        choice.canToggle = true;
                        bool ok = TogglePluginConfig(gGames[gSelectedGame], choice);
                        char notice[sizeof(gStatus)];
                        CopyText(notice, sizeof(notice), gStatus);
                        LoadConfigEntries();
                        int enabledCount = 0;
                        for (int gi = 0; gi < gGameCount; ++gi)
                        {
                            gBulkSelected[gi] = EffectivePluginState(choice.path, gGames[gi].id, NULL);
                            if (gBulkSelected[gi]) ++enabledCount;
                        }
                        gBulkSelectedCount = enabledCount;
                        if (ok) snprintf(gStatus, sizeof(gStatus), "%s for %s", gBulkSelected[gSelectedGame] ? "Enabled" : "Disabled", gGames[gSelectedGame].title);
                        else CopyText(gStatus, sizeof(gStatus), notice);
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { gBulkView = 0; gBulkSelectedCount = 0; memset(gBulkSelected, 0, sizeof(gBulkSelected)); }
            }
            DrawGamePlugins(scene, largeFont, smallFont);
        }
        else
        {
            if (gIniKeyboard)
            {
                int keyCount = (int)strlen(INI_KEYBOARD_CHARS);
                const int cols = 12;
                if ((pressed & ORBIS_PAD_BUTTON_LEFT) && gIniKeyboardCursor > 0) --gIniKeyboardCursor;
                if ((pressed & ORBIS_PAD_BUTTON_RIGHT) && gIniKeyboardCursor + 1 < keyCount) ++gIniKeyboardCursor;
                if ((pressed & ORBIS_PAD_BUTTON_UP) && gIniKeyboardCursor >= cols) gIniKeyboardCursor -= cols;
                if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gIniKeyboardCursor + cols < keyCount) gIniKeyboardCursor += cols;
                if (pressed & ORBIS_PAD_BUTTON_CROSS)
                {
                    size_t len = strlen(gIniEditBuffer);
                    if (len + 1 < sizeof(gIniEditBuffer))
                    {
                        gIniEditBuffer[len] = INI_KEYBOARD_CHARS[gIniKeyboardCursor];
                        gIniEditBuffer[len + 1] = '\0';
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_SQUARE)
                {
                    size_t len = strlen(gIniEditBuffer);
                    if (len) gIniEditBuffer[len - 1] = '\0';
                }
                if (pressed & ORBIS_PAD_BUTTON_TRIANGLE)
                {
                    if (CommitIniEdit()) gIniKeyboard = false;
                }
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { gIniKeyboard = false; snprintf(gStatus, sizeof(gStatus), "Edit cancelled"); }
                DrawIniKeyboard(scene, largeFont, smallFont);
            }
            else if (gIniBackupView)
            {
                if ((pressed & ORBIS_PAD_BUTTON_UP) && gIniBackupCursor > 0) --gIniBackupCursor;
                if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gIniBackupCursor + 1 < gIniBackupCount) ++gIniBackupCursor;
                if (pressed & ORBIS_PAD_BUTTON_CROSS) RestoreSelectedIniBackup();
                if (pressed & ORBIS_PAD_BUTTON_CIRCLE) gIniBackupView = false;
                if (pressed & ORBIS_PAD_BUTTON_SQUARE) DeleteSelectedIniBackup();
                DrawIniBackups(scene, largeFont, smallFont);
            }
            else
            {
                if (gConfigLineCount > 0)
                {
                    if ((pressed & ORBIS_PAD_BUTTON_UP) && gIniCursor > 0) --gIniCursor;
                    if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gIniCursor + 1 < gConfigLineCount) ++gIniCursor;
                }
                if (pressed & ORBIS_PAD_BUTTON_CROSS) EditSelectedIniLine(userID);
                if (pressed & ORBIS_PAD_BUTTON_SQUARE) DeleteSelectedIniLine();
                if (pressed & ORBIS_PAD_BUTTON_L2)
                {
                    if (gIniCursor >= 0 && gIniCursor < gConfigLineCount)
                    {
                        CopyText(gIniClipboard, sizeof(gIniClipboard), gConfigLines[gIniCursor]);
                        gIniClipboardValid = true;
                        snprintf(gStatus, sizeof(gStatus), "Copied line %d", gIniCursor + 1);
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_R2)
                {
                    if (!gIniClipboardValid) snprintf(gStatus, sizeof(gStatus), "Clipboard is empty; press L2 on a line to copy");
                    else if (gConfigLineCount >= MAX_CONFIG_LINES) snprintf(gStatus, sizeof(gStatus), "plugins.ini is full; paste cancelled");
                    else
                    {
                        if (gIniCursor >= 0 && gIniCursor < gConfigLineCount)
                        {
                            for (int i = gConfigLineCount; i > gIniCursor + 1; --i)
                                CopyText(gConfigLines[i], CONFIG_LINE_SIZE, gConfigLines[i - 1]);
                            CopyText(gConfigLines[gIniCursor + 1], CONFIG_LINE_SIZE, gIniClipboard);
                            ++gConfigLineCount;
                            if (SaveConfigLinesSafely(false)) { ++gIniCursor; snprintf(gStatus, sizeof(gStatus), "Pasted copied line"); }
                            else LoadConfigLines();
                        }
                    }
                }
                if (pressed & ORBIS_PAD_BUTTON_TRIANGLE)
                {
                    if (CreateTimestampedBackup())
                    {
                        RefreshIniBackups();
                        snprintf(gStatus, sizeof(gStatus), "Backup created; %d backups available", gIniBackupCount);
                    }
                    else snprintf(gStatus, sizeof(gStatus), "Backup failed (plugins.ini may not exist yet)");
                }
                if (pressed & ORBIS_PAD_BUTTON_OPTIONS)
                {
                    RefreshIniBackups();
                    gIniBackupView = true;
                }
                if (pressed & ORBIS_PAD_BUTTON_L3)
                {
                    LoadConfigLines(); LoadConfigEntries();
                    snprintf(gStatus, sizeof(gStatus), "Reloaded %d lines", gConfigLineCount);
                }
                DrawPluginsIni(scene, largeFont, smallFont);
            }
        }

        scene->SubmitFlip(frameID);
        scene->FrameWait(frameID);
        scene->FrameBufferSwap();
        ++frameID;
    }
    return 0;
}
