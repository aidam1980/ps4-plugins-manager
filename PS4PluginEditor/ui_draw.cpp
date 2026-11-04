#include "app_state.h"
#include "button_icons_embedded.h"
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
void DrawImage(Scene2D *scene, const PngImage &image, int x, int y, int w, int h, Color background)
{
    if (!scene || !image.rgba || image.width <= 0 || image.height <= 0 || w <= 0 || h <= 0) return;
    scene->DrawRGBAImage(image.rgba, image.width, image.height, x, y, w, h, background);
}

void DrawTopBar(Scene2D *scene, FT_Face largeFont, FT_Face smallFont, const char *title, const char *subtitle)
{
    if (!scene) return;
    (void)largeFont;
    scene->DrawRectangle(0, 0, SCREEN_W, 54, HEADER);
    // A restrained accent line separates the application chrome from content.
    scene->DrawRectangle(0, 53, SCREEN_W, 1, ACCENT);
    if (title && smallFont)
        scene->DrawText((char *)title, smallFont, 62, 36, HEADER, WHITE);
    if (subtitle && smallFont)
        scene->DrawText((char *)subtitle, smallFont, 820, 36, HEADER, MUTED);
}

void DrawPanel(Scene2D *scene, int x, int y, int w, int h, Color fill)
{
    if (!scene || w < 5 || h < 5) return;
    // Flat surface with a fine outline: no nested frames or glossy highlights.
    scene->DrawRectangle(x, y, w, h, fill);
    scene->DrawRectangle(x, y, w, 1, PANEL_EDGE);
    scene->DrawRectangle(x, y + h - 1, w, 1, PANEL_EDGE);
    scene->DrawRectangle(x, y, 1, h, PANEL_EDGE);
    scene->DrawRectangle(x + w - 1, y, 1, h, PANEL_EDGE);
}

void DrawFooterKey(Scene2D *scene, FT_Face smallFont, const char *label,
                          int x, int y, int w, int h)
{
    DrawPanel(scene, x, y, w, h, KEY_BG);
    scene->DrawText((char *)label, smallFont, x + 10, y + 38, KEY_BG, WHITE);
}

void DrawFooter(Scene2D *scene, FT_Face smallFont, const char *controls)
{
    // Floating footer cards: PayLoader notifications are system overlays and are
    // intentionally not drawn or modified here.
    const int panelY = 934;
    const int keyY = 954;
    const int textY = 991;
    const int leftX = 48;

    DrawPanel(scene, leftX, panelY, 1060, 98, PANEL_BG);

    if (controls && strstr(controls, "UP/DOWN"))
    {
        DrawFooterKey(scene, smallFont, "UP/DN", 72, keyY, 94, 58);
        scene->DrawText((char *)"SELECT FILE", smallFont, 179, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(328, 955, 2, 55, DIVIDER);

        DrawFooterKey(scene, smallFont, "X", 351, keyY, 49, 58);
        scene->DrawText((char *)"TOGGLE INI", smallFont, 414, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(565, 955, 2, 55, DIVIDER);

        DrawFooterKey(scene, smallFont, "O", 608, keyY, 49, 58);
        scene->DrawText((char *)"BACK", smallFont, 669, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(756, 955, 2, 55, DIVIDER);

        DrawFooterKey(scene, smallFont, "SQR", 778, keyY, 57, 58);
        scene->DrawText((char *)"RESCAN", smallFont, 846, textY, PANEL_BG, WHITE);
    }
    else
    {
        DrawFooterKey(scene, smallFont, "ARROWS", 72, keyY, 108, 58);
        scene->DrawText((char *)"MOVE", smallFont, 195, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(294, 955, 2, 55, DIVIDER);

        DrawFooterKey(scene, smallFont, "X", 319, keyY, 49, 58);
        scene->DrawText((char *)"OPEN GAME", smallFont, 382, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(566, 955, 2, 55, DIVIDER);

        DrawFooterKey(scene, smallFont, "SQR", 590, keyY, 57, 58);
        scene->DrawText((char *)"REFRESH", smallFont, 658, textY, PANEL_BG, WHITE);
    }

}

bool LoadButtonAsset(const char *name, PngImage *image)
{
    if (!name || !image) return false;
    // Clear any stale state before attempting a load. The button art is part of
    // app0, so use the mounted application path first, then common CWD variants.
    image->width = image->height = 0;
    image->rgba = NULL;
    char path[256];
    const char *formats[] = {
        "/app0/assets/buttons/%s",
        "assets/buttons/%s",
        "./assets/buttons/%s",
        "/app0/PS4PluginEditor/assets/buttons/%s"
    };
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
    {
        snprintf(path, sizeof(path), formats[i], name);
        if (LoadPngRGBA(path, image) && image->rgba && image->width > 0 && image->height > 0)
        {
            printf("[button-icon] loaded %s from %s (%dx%d)\n", name, path, image->width, image->height);
            return true;
        }
    }
    printf("[button-icon] FAILED to load %s\n", name);
    return false;
}

void DrawMyGamesFooter(Scene2D *scene, FT_Face font)
{
    // Use the same embedded, transparent controller artwork as the other
    // legends. Do not runtime-load PNGs here: app0 paths are not reliable on
    // every launch/build, which caused the My Games footer to show text only.
    static PngImage cross = {128, 128, (unsigned char *)kButton_cross};
    static PngImage triangle = {128, 128, (unsigned char *)kButton_triangle};
    static PngImage l1 = {128, 128, (unsigned char *)kButton_l1};
    static PngImage r1 = {128, 128, (unsigned char *)kButton_r1};

    DrawPanel(scene, 48, 934, 1824, 98, PANEL_BG);
    const int iconY = 947;
    const int labelY = 1009;

    // Three evenly spaced groups, with crisp 48px controller icons above text.
    DrawImage(scene, cross, 100, iconY - 3, 56, 56, PANEL_BG);
    scene->DrawText((char *)"Select Plugin", font, 164, labelY, PANEL_BG, WHITE);

    DrawImage(scene, triangle, 581, iconY - 3, 56, 56, PANEL_BG);
    scene->DrawText((char *)"Show Selected Plugins", font, 645, labelY, PANEL_BG, WHITE);

    DrawImage(scene, l1, 1184, iconY, 80, 50, PANEL_BG);
    DrawImage(scene, r1, 1264, iconY, 80, 50, PANEL_BG);
    scene->DrawText((char *)"Switch List", font, 1332, labelY, PANEL_BG, WHITE);
}

bool LoadButtonAsset(const char *name, PngImage *image);
struct LegendButton { const char *button; const char *action; };
void DrawButtonLegend(Scene2D *scene, FT_Face font, const LegendButton *items, int count);

void DrawNavFooter(Scene2D *scene, FT_Face font, const char *controls)
{
    // These screens used to print one long text string, so no controller PNGs
    // were ever drawn there. Route each known footer through the same image
    // legend renderer used by the plugin lists.
    if (controls && strstr(controls, "TOGGLE PLUGIN"))
    {
        const LegendButton legend[] = {
            {"ARROWS", "MOVE"}, {"X", "TOGGLE PLUGIN"}, {"O", "BACK"}
        };
        DrawButtonLegend(scene, font, legend, 3);
        return;
    }
    if (controls && strstr(controls, "TOGGLE GAME"))
    {
        const LegendButton legend[] = {
            {"ARROWS", "MOVE"}, {"X", "TOGGLE GAME"},
            {"TRIANGLE", "APPLY"}, {"O", "BACK"}
        };
        DrawButtonLegend(scene, font, legend, 4);
        return;
    }
    if (controls && strstr(controls, "BACKSPACE"))
    {
        const LegendButton legend[] = {
            {"ARROWS", "MOVE"}, {"X", "INSERT"},
            {"SQUARE", "BACKSPACE"}, {"TRIANGLE", "SAVE"}, {"O", "CANCEL"}
        };
        DrawButtonLegend(scene, font, legend, 5);
        return;
    }
    DrawPanel(scene, 48, 934, 1824, 98, PANEL_BG);
}

// Button legends for navigation lists. Draw the PNG artwork when available;
// otherwise use an ASCII label so the control is never invisible on the PS4
// font renderer (which may not contain Unicode controller glyphs).
void DrawButtonLegend(Scene2D *scene, FT_Face font, const LegendButton *items, int count)
{
    // High-resolution embedded controller artwork; render larger and align each
    // icon/action as a centered, self-contained group to prevent collisions.
    static PngImage cross = {128, 128, (unsigned char *)kButton_cross};
    static PngImage circle = {128, 128, (unsigned char *)kButton_circle};
    static PngImage square = {128, 128, (unsigned char *)kButton_square};
    static PngImage triangle = {128, 128, (unsigned char *)kButton_triangle};
    static PngImage l1 = {128, 128, (unsigned char *)kButton_l1};
    static PngImage r1 = {128, 128, (unsigned char *)kButton_r1};
    static PngImage l2 = {128, 128, (unsigned char *)kButton_l2};
    static PngImage r2 = {128, 128, (unsigned char *)kButton_r2};
    static PngImage r3 = {128, 128, (unsigned char *)kButton_r3};
    static PngImage options = {128, 128, (unsigned char *)kButton_options};
    static PngImage arrows = {128, 128, (unsigned char *)kButton_arrows};
    DrawPanel(scene, 48, 934, 1824, 98, PANEL_BG);
    if (count < 1) return;
    const int startX = 68, totalW = 1784, gap = 12;
    const int itemW = (totalW - gap * (count - 1)) / count;
    const int iconY = 939;
    for (int i = 0; i < count; ++i)
    {
        const int x = startX + i * (itemW + gap);
        const char *b = items[i].button;
        PngImage *asset = NULL;
        if (!strcmp(b, "X")) asset = &cross;
        else if (!strcmp(b, "O") || !strcmp(b, "CIRCLE")) asset = &circle;
        else if (!strcmp(b, "SQUARE")) asset = &square;
        else if (!strcmp(b, "TRIANGLE")) asset = &triangle;
        else if (!strcmp(b, "L1")) asset = &l1;
        else if (!strcmp(b, "R1")) asset = &r1;
        else if (!strcmp(b, "L2")) asset = &l2;
        else if (!strcmp(b, "R2")) asset = &r2;
        else if (!strcmp(b, "R3")) asset = &r3;
        else if (!strcmp(b, "OPTIONS")) asset = &options;
        else if (!strcmp(b, "ARROWS")) asset = &arrows;
        if (!strcmp(b, "L1R1"))
        {
            // Combined shoulder-button hint for Switch List.
            DrawImage(scene, l1, x + itemW / 2 - 60, iconY + 4, 56, 36, PANEL_BG);
            DrawImage(scene, r1, x + itemW / 2 + 4, iconY + 4, 56, 36, PANEL_BG);
        }
        else if (asset && asset->rgba)
        {
            int iconW = 64, iconH = 64;
            if (!strcmp(b, "L1") || !strcmp(b, "R1")) { iconW = 88; iconH = 56; }
            else if (!strcmp(b, "L2") || !strcmp(b, "R2")) { iconW = 72; iconH = 48; }
            else if (!strcmp(b, "OPTIONS")) { iconW = 54; iconH = 54; }
            else if (!strcmp(b, "ARROWS")) { iconW = 56; iconH = 56; }
            DrawImage(scene, *asset, x + (itemW - iconW) / 2, iconY + (64 - iconH) / 2, iconW, iconH, PANEL_BG);
        }
        // Approximate glyph width from the selected FreeType pixel size and
        // center the action beneath its icon, clamped to the group's bounds.
        int px = (font && font->size) ? (int)font->size->metrics.x_ppem : 20;
        if (px < 12) px = 12;
        int textW = (int)strlen(items[i].action) * px * 54 / 100;
        if (textW > itemW - 8) textW = itemW - 8;
        int textX = x + (itemW - textW) / 2;
        scene->DrawText((char *)items[i].action, font, textX, 1011, PANEL_BG, WHITE);
    }
}

void DrawAppChrome(Scene2D *scene, FT_Face largeFont, FT_Face smallFont,
                          const char *subtitle)
{
    scene->FrameBufferFill(BG);
    DrawTopBar(scene, largeFont, smallFont, "Plugin Manager", subtitle);
    const char *tabs[3] = {"MY GAMES", "GAME PLUGINS", "plugins.ini"};
    const int xs[3] = {62, 670, 1278};
    for (int i = 0; i < 3; ++i)
    {
        Color fill = (gTab == i) ? CARD_ALT : PANEL_BG;
        DrawPanel(scene, xs[i], 66, 580, 58, fill);
        if (gTab == i)
            scene->DrawRectangle(xs[i] + 22, 119, 92, 3, ACCENT);
        scene->DrawText((char *)tabs[i], smallFont, xs[i] + 24, 103, fill,
                        (gTab == i) ? WHITE : MUTED);
    }
}

void DrawGameGrid(Scene2D *scene, FT_Face smallFont, bool multiSelect)
{
    const int pageStart = (gSelectedGame / GAME_PAGE_SIZE) * GAME_PAGE_SIZE;
    LoadPageIcons(pageStart);
    const int cardW = 590, cardH = 220, startX = 55, startY = 174;
    const int gapX = 20, gapY = 12;
    for (int slot = 0; slot < GAME_PAGE_SIZE; ++slot)
    {
        int index = pageStart + slot;
        if (index >= gGameCount) break;
        int x = startX + (slot % 3) * (cardW + gapX);
        int y = startY + (slot / 3) * (cardH + gapY);
        bool selected = index == gSelectedGame;
        Color fill = selected ? CARD_ALT : CARD;
        DrawPanel(scene, x, y, cardW, cardH, fill);
        if (selected) scene->DrawRectangle(x + 3, y + 3, 4, cardH - 6, ACCENT);
        PngImage &icon = gPageIcons[slot];
        const int iconBox = 204;
        const int iconX = x + 16, iconY = y + 14;
        if (icon.rgba && icon.width > 0 && icon.height > 0)
        {
            float scale = (float)iconBox / (float)(icon.width > icon.height ? icon.width : icon.height);
            int drawW = (int)(icon.width * scale), drawH = (int)(icon.height * scale);
            DrawImage(scene, icon, iconX + (iconBox - drawW) / 2, iconY + (iconBox - drawH) / 2,
                      drawW, drawH, fill);
        }
        else
        {
            scene->DrawRectangle(iconX, iconY, iconBox, iconBox, HEADER);
            scene->DrawText((char *)"NO ICON", smallFont, iconX + 30, iconY + 81, HEADER, MUTED);
        }
        char displayTitle[96];
        ShortenText(gGames[index].title, displayTitle, sizeof(displayTitle), 38);
        scene->DrawText(displayTitle, smallFont, x + 232, y + 68, fill, WHITE);
        if (strcasecmp(gGames[index].title, gGames[index].id) != 0)
            scene->DrawText(gGames[index].id, smallFont, x + 232, y + 99, fill, MUTED);
        if (!multiSelect)
            scene->DrawText((char *)"Select Plugin", smallFont, x + 232, y + 148, fill,
                            selected ? ACCENT : MUTED);
        if (multiSelect)
        {
            const char *mark = gBulkSelected[index] ? "PLUGIN ENABLED" : "PLUGIN DISABLED";
            scene->DrawText((char *)mark, smallFont, x + 232, y + 148, fill,
                            gBulkSelected[index] ? GREEN : (selected ? ACCENT : MUTED));
        }
    }
    if (gGameCount > GAME_PAGE_SIZE)
    {
        char pageText[64];
        snprintf(pageText, sizeof(pageText), "PAGE %d / %d", pageStart / GAME_PAGE_SIZE + 1,
                 (gGameCount + GAME_PAGE_SIZE - 1) / GAME_PAGE_SIZE);
        scene->DrawText(pageText, smallFont, 1630, 895, BG, MUTED);
    }
}

void DrawMyGames(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    char subtitle[160];
    snprintf(subtitle, sizeof(subtitle), "%d games found in install roots", gGameCount);
    DrawAppChrome(scene, largeFont, smallFont,
                  gInDetails && gGameCount > 0 ? "MY GAMES  /  GAME PLUGINS" : "PS4 PLUGIN MANAGER");
    if (!gInDetails)
    {
        char heading[128];
        snprintf(heading, sizeof(heading), "MY GAMES  -  %d found", gGameCount);
        scene->DrawText(heading, smallFont, 70, 150, BG, WHITE);
        if (gGameCount > 0) DrawGameGrid(scene, smallFont, false);
        else
        {
            scene->DrawText((char *)"No installed game folders found", largeFont, 160, 405, BG, WHITE);
            scene->DrawText((char *)"Scan status:", smallFont, 165, 475, BG, MUTED);
            scene->DrawText(gStatus, smallFont, 165, 525, BG, AMBER);
        }
        DrawMyGamesFooter(scene, smallFont);
        if (gShowSelectedPlugins && gGameCount > 0)
        {
            const Game &selectedGame = gGames[gSelectedGame];
            LoadConfigEntries();
            DrawPanel(scene, 510, 300, 900, 500, PANEL_BG);
            scene->DrawText((char *)"SELECTED", largeFont, 570, 360, PANEL_BG, WHITE);
            int lineY = 415, count = 0;
            for (int ci = 0; ci < gConfigCount && count < 10; ++ci)
            {
                if (!IsDefaultSection(gConfig[ci].section) &&
                    strcasecmp(gConfig[ci].section, selectedGame.id) != 0) continue;
                bool active = EffectivePluginState(gConfig[ci].path, selectedGame.id, NULL);
                if (!active) continue;
                const char *base = strrchr(gConfig[ci].path, '/');
                base = base ? base + 1 : gConfig[ci].path;
                bool duplicate = false;
                for (int prev = 0; prev < ci; ++prev)
                {
                    if (!IsDefaultSection(gConfig[prev].section) &&
                        strcasecmp(gConfig[prev].section, selectedGame.id) != 0) continue;
                    const char *pb = strrchr(gConfig[prev].path, '/'); pb = pb ? pb + 1 : gConfig[prev].path;
                    if (strcmp(pb, base) == 0 &&
                        EffectivePluginState(gConfig[prev].path, selectedGame.id, NULL)) { duplicate = true; break; }
                }
                if (duplicate) continue;
                scene->DrawText((char *)base, smallFont, 590, lineY, PANEL_BG, GREEN);
                lineY += 36; ++count;
            }
            if (count == 0)
                scene->DrawText((char *)"No enabled plugins in this game's section.", smallFont, 590, lineY, PANEL_BG, MUTED);
            scene->DrawText((char *)"CIRCLE: CLOSE", smallFont, 590, 750, PANEL_BG, WHITE);
        }
        return;
    }

    const Game &game = gGames[gSelectedGame];
    char heading[140];
    snprintf(heading, sizeof(heading), "%s  /  PLUGINS", game.id);
    scene->DrawText(heading, smallFont, 70, 150, BG, WHITE);
    DrawPanel(scene, 55, 238, 1810, 640, PANEL_BG);
    scene->DrawText((char *)"PLUGIN FILE", smallFont, 95, 274, PANEL_BG, MUTED);
    scene->DrawText((char *)"STATE FOR THIS GAME", smallFont, 1515, 274, PANEL_BG, MUTED);
    const int visible = 9;
    if (gSelectedPlugin < gPluginScroll) gPluginScroll = gSelectedPlugin;
    if (gSelectedPlugin >= gPluginScroll + visible) gPluginScroll = gSelectedPlugin - visible + 1;
    if (gPluginScroll < 0) gPluginScroll = 0;
    if (gPluginCount == 0)
    {
        scene->DrawText((char *)"No .prx/.sprx files found in /data/GoldHEN/plugins", smallFont, 95, 360, PANEL_BG, MUTED);
    }
    for (int row = 0; row < visible; ++row)
    {
        int idx = gPluginScroll + row;
        if (idx >= gPluginCount) break;
        int y = 302 + row * 61;
        bool selected = idx == gSelectedPlugin;
        Color rowBg = selected ? CARD_ALT : PANEL_BG;
        if (selected)
        {
            scene->DrawRectangle(75, y - 9, 1760, 54, CARD_ALT);
            scene->DrawRectangle(75, y - 9, 3, 54, ACCENT);
        }
        char path[132];
        ShortenPath(gPlugins[idx].path, path, sizeof(path), 94);
        scene->DrawText(path, smallFont, 95, y + 14, rowBg, selected ? WHITE : MUTED);
        scene->DrawText((char *)(gPlugins[idx].enabled ? "ENABLED" : "DISABLED"),
                        smallFont, 1535, y + 14, rowBg, gPlugins[idx].enabled ? GREEN : WHITE);
    }
    scene->DrawText(gStatus, smallFont, 75, 902, BG, AMBER);
    const LegendButton legend[] = {{"ARROWS", "SELECT FILE"}, {"X", "TOGGLE"}, {"O", "BACK"}, {"L1", "PREV LIST"}, {"R1", "NEXT LIST"}};
    DrawButtonLegend(scene, smallFont, legend, 5);
}

void DrawGamePlugins(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "PLUGIN FILES  /  ASSIGN TO ONE OR MORE GAMES");
    if (gBulkView == 2)
    {
        char heading[150];
        snprintf(heading, sizeof(heading), "PLUGIN STATUS BY GAME  •  ENABLED IN %d GAMES", gBulkSelectedCount);
        scene->DrawText(heading, smallFont, 70, 150, BG, WHITE);
        if (gGameCount > 0) DrawGameGrid(scene, smallFont, true);
        else scene->DrawText((char *)"No games found; cannot assign this plugin.", largeFont, 200, 420, BG, AMBER);
        DrawNavFooter(scene, smallFont, "ARROWS MOVE   X TOGGLE PLUGIN FOR THIS GAME   O BACK");
        return;
    }

    scene->DrawText((char *)(gPluginGridView ? "MY PLUGINS  •  GRID VIEW" : "MY PLUGINS  •  LIST VIEW"), smallFont, 70, 220, BG, WHITE);
    if (gPluginGridView)
    {
        const int perPage = 9;
        int pageStart = (gSelectedPlugin / perPage) * perPage;
        const int cardW = 590, cardH = 205, startX = 55, startY = 238, gapX = 20, gapY = 12;
        for (int slot = 0; slot < perPage; ++slot)
        {
            int idx = pageStart + slot;
            if (idx >= gPluginCount) break;
            int x = startX + (slot % 3) * (cardW + gapX);
            int y = startY + (slot / 3) * (cardH + gapY);
            bool selected = idx == gSelectedPlugin;
            Color fill = selected ? CARD_ALT : CARD;
            DrawPanel(scene, x, y, cardW, cardH, fill);
            if (selected) scene->DrawRectangle(x + 3, y + 3, 4, cardH - 6, ACCENT);
            char imagePath[640];
            char imageName[256];
            const char *pluginBase = strrchr(gPlugins[idx].path, '/');
            pluginBase = pluginBase ? pluginBase + 1 : gPlugins[idx].path;
            CopyText(imageName, sizeof(imageName), pluginBase);
            char *dot = strrchr(imageName, '.');
            if (dot) *dot = '\0';
            strncat(imageName, ".png", sizeof(imageName) - strlen(imageName) - 1);

            // Accept the image beside the plugin, in the plugin root, or in its images folder.
            // This supports both per-folder and shared icon collections.
            PngImage image = {};
            CopyText(imagePath, sizeof(imagePath), gPlugins[idx].path);
            dot = strrchr(imagePath, '.');
            if (dot) *dot = '\0';
            strncat(imagePath, ".png", sizeof(imagePath) - strlen(imagePath) - 1);
            bool hasImage = LoadPngRGBA(imagePath, &image);
            if (!hasImage)
            {
                snprintf(imagePath, sizeof(imagePath), "%s/%s", PLUGIN_ROOT, imageName);
                hasImage = LoadPngRGBA(imagePath, &image);
            }
            if (!hasImage)
            {
                snprintf(imagePath, sizeof(imagePath), "%s/images/%s", PLUGIN_ROOT, imageName);
                hasImage = LoadPngRGBA(imagePath, &image);
            }
            const int box = 154, ix = x + 16, iy = y + 18;
            if (hasImage && image.rgba && image.width > 0 && image.height > 0)
            {
                float scale = (float)box / (float)(image.width > image.height ? image.width : image.height);
                int dw = (int)(image.width * scale), dh = (int)(image.height * scale);
                DrawImage(scene, image, ix + (box - dw) / 2, iy + (box - dh) / 2, dw, dh, fill);
                free(image.rgba);
            }
            else
            {
                scene->DrawRectangle(ix, iy, box, box, HEADER);
                scene->DrawRectangle(ix + 8, iy + 8, box - 16, box - 16, PANEL_EDGE);
                scene->DrawText((char *)"NO MEDIA", smallFont, ix + 20, iy + 82, HEADER, MUTED);
            }
            const char *base = strrchr(gPlugins[idx].path, '/');
            base = base ? base + 1 : gPlugins[idx].path;
            scene->DrawText((char *)base, smallFont, x + 188, y + 67, fill, selected ? WHITE : MUTED);
            const char *state = gPlugins[idx].enabled ? "DEFAULT ON" : (gPlugins[idx].configured ? "DEFAULT OFF" : "PER-GAME ONLY");
            scene->DrawText((char *)state, smallFont, x + 188, y + 110, fill, gPlugins[idx].enabled ? GREEN : MUTED);
        }
        if (gPluginCount > perPage)
        {
            char pageText[64];
            snprintf(pageText, sizeof(pageText), "PAGE %d / %d", pageStart / perPage + 1, (gPluginCount + perPage - 1) / perPage);
            scene->DrawText(pageText, smallFont, 1630, 895, BG, MUTED);
        }
        scene->DrawText(gStatus, smallFont, 75, 902, BG, AMBER);
        const LegendButton legend[] = {{"ARROWS", "SELECT PLUGIN"}, {"X", "ASSIGN TO GAMES"}, {"OPTIONS", "SET DEFAULT"}, {"R3", "LIST / GRID"}, {"SQUARE", "REFRESH"}, {"L1R1", "SWITCH LIST"}};
        DrawButtonLegend(scene, smallFont, legend, 6);
        return;
    }
    DrawPanel(scene, 55, 238, 1810, 640, PANEL_BG);
    scene->DrawText((char *)"PLUGIN FILE", smallFont, 95, 274, PANEL_BG, MUTED);
    scene->DrawText((char *)"DEFAULT STATE", smallFont, 1535, 274, PANEL_BG, MUTED);
    const int visible = 9;
    if (gSelectedPlugin < gPluginScroll) gPluginScroll = gSelectedPlugin;
    if (gSelectedPlugin >= gPluginScroll + visible) gPluginScroll = gSelectedPlugin - visible + 1;
    if (gPluginScroll < 0) gPluginScroll = 0;
    if (gPluginCount == 0)
        scene->DrawText((char *)"No .prx/.sprx files found in this folder.", smallFont, 95, 360, PANEL_BG, MUTED);
    for (int row = 0; row < visible; ++row)
    {
        int idx = gPluginScroll + row;
        if (idx >= gPluginCount) break;
        int y = 302 + row * 61;
        bool selected = idx == gSelectedPlugin;
        Color rowBg = selected ? CARD_ALT : PANEL_BG;
        if (selected)
        {
            scene->DrawRectangle(75, y - 9, 1760, 54, CARD_ALT);
            scene->DrawRectangle(75, y - 9, 3, 54, ACCENT);
        }
        char path[132];
        ShortenPath(gPlugins[idx].path, path, sizeof(path), 94);
        scene->DrawText(path, smallFont, 95, y + 14, rowBg, selected ? WHITE : MUTED);
        const char *state = gPlugins[idx].enabled ? "DEFAULT ON" : (gPlugins[idx].configured ? "DEFAULT OFF" : "PER-GAME ONLY");
        Color stateColor = gPlugins[idx].enabled ? GREEN : MUTED;
        scene->DrawText((char *)state, smallFont, 1535, y + 14, rowBg, stateColor);
    }
    scene->DrawText(gStatus, smallFont, 75, 902, BG, AMBER);
    const LegendButton legend[] = {{"ARROWS", "SELECT"}, {"X", "ASSIGN TO GAMES"}, {"OPTIONS", "SET DEFAULT"}, {"R3", "LIST / GRID"}, {"SQUARE", "REFRESH"}, {"L1R1", "SWITCH LIST"}};
    DrawButtonLegend(scene, smallFont, legend, 6);
}

void DrawPluginsIni(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "LIVE VIEW  /  /data/GoldHEN/plugins.ini");
    char heading[100];
    snprintf(heading, sizeof(heading), "PLUGINS.INI  •  %d LINES", gConfigLineCount);
    scene->DrawText(heading, smallFont, 70, 150, BG, WHITE);
    DrawPanel(scene, 55, 238, 1810, 640, PANEL_BG);
    if (gConfigLineCount == 0)
    {
        scene->DrawText((char *)"File is missing, empty, or unreadable.", smallFont, 95, 310, PANEL_BG, AMBER);
        scene->DrawText((char *)PLUGINS_INI, smallFont, 95, 370, PANEL_BG, MUTED);
    }
    const int visible = 10;
    if (gIniCursor < gIniScroll) gIniScroll = gIniCursor;
    if (gIniCursor >= gIniScroll + visible) gIniScroll = gIniCursor - visible + 1;
    if (gIniScroll < 0) gIniScroll = 0;
    for (int row = 0; row < visible; ++row)
    {
        int idx = gIniScroll + row;
        if (idx >= gConfigLineCount) break;
        int y = 280 + row * 56;
        char line[CONFIG_LINE_SIZE];
        CopyText(line, sizeof(line), gConfigLines[idx]);
        Trim(line);
        bool selected = idx == gIniCursor;
        Color rowBg = selected ? CARD_ALT : PANEL_BG;
        if (selected)
        {
            scene->DrawRectangle(72, y - 5, 1772, 48, CARD_ALT);
            scene->DrawRectangle(72, y - 5, 3, 48, ACCENT);
        }
        Color textColor = WHITE;
        char section[64];
        char path[512];
        bool enabled = false;
        if (ParseSectionName(line, section, sizeof(section)))
            textColor = IsTitleIdFolderName(section) ? GREEN : MUTED;
        else if (ParsePluginLine(line, path, sizeof(path), &enabled))
            textColor = FileExists(path) ? WHITE : RED;
        else if (line[0] == '/')
            textColor = RED; // malformed plugin path or unsupported extension
        char shown[180];
        ShortenText(line, shown, sizeof(shown), 145);
        char numbered[210];
        snprintf(numbered, sizeof(numbered), "%04d  %s", idx + 1, shown);
        scene->DrawText(numbered, smallFont, 95, y + 22, rowBg, textColor);
    }
    scene->DrawText(gStatus, smallFont, 75, 902, BG, MUTED);
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "EDIT"}, {"SQUARE", "DELETE"}, {"L2", "COPY"}, {"R2", "PASTE"}, {"TRIANGLE", "BACKUP"}, {"OPTIONS", "HISTORY"}};
    DrawButtonLegend(scene, smallFont, legend, 7);
}

void DrawIniKeyboard(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "EDIT SELECTED PLUGINS.INI LINE");
    DrawPanel(scene, 70, 245, 1780, 620, PANEL_BG);
    scene->DrawText((char *)"LINE TEXT", smallFont, 100, 292, PANEL_BG, ACCENT);
    char shown[180];
    ShortenText(gIniEditBuffer, shown, sizeof(shown), 145);
    scene->DrawText(shown, smallFont, 100, 340, PANEL_BG, WHITE);
    const int cols = 12;
    const int cellW = 132;
    const int cellH = 58;
    const int startX = 100;
    const int startY = 390;
    int count = (int)strlen(INI_KEYBOARD_CHARS);
    int rows = (count + cols - 1) / cols;
    for (int i = 0; i < count; ++i)
    {
        int x = startX + (i % cols) * cellW;
        int y = startY + (i / cols) * cellH;
        bool selected = i == gIniKeyboardCursor;
        if (selected) scene->DrawRectangle(x, y, cellW - 8, cellH - 7, ACCENT);
        char label[8] = {};
        label[0] = INI_KEYBOARD_CHARS[i] == ' ' ? '_' : INI_KEYBOARD_CHARS[i];
        scene->DrawText(label, smallFont, x + 18, y + 34, selected ? ACCENT : PANEL_BG, WHITE);
    }
    scene->DrawText((char *)"X: INSERT CHAR   SQUARE: BACKSPACE   TRIANGLE: SAVE   CIRCLE: CANCEL", smallFont, 100, startY + rows * cellH + 12, PANEL_BG, MUTED);
    DrawNavFooter(scene, smallFont, "D-PAD MOVE   X INSERT   SQUARE BACKSPACE   TRIANGLE SAVE   CIRCLE CANCEL");
}

void DrawIniBackups(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "PLUGINS.INI BACKUP HISTORY");
    DrawPanel(scene, 55, 238, 1810, 640, PANEL_BG);
    scene->DrawText((char *)"BACKUPS — NEWEST FIRST", smallFont, 95, 270, PANEL_BG, ACCENT);
    const int visible = 10;
    if (gIniBackupCursor < gIniBackupScroll) gIniBackupScroll = gIniBackupCursor;
    if (gIniBackupCursor >= gIniBackupScroll + visible) gIniBackupScroll = gIniBackupCursor - visible + 1;
    if (gIniBackupScroll < 0) gIniBackupScroll = 0;
    if (gIniBackupCount == 0)
        scene->DrawText((char *)"No backups found in /data/GoldHEN/plugins.ini_backups", smallFont, 100, 330, PANEL_BG, AMBER);
    for (int row = 0; row < visible; ++row)
    {
        int idx = gIniBackupScroll + row;
        if (idx >= gIniBackupCount) break;
        int y = 310 + row * 52;
        bool selected = idx == gIniBackupCursor;
        if (selected) scene->DrawRectangle(82, y - 8, 1730, 45, CARD_ALT);
        scene->DrawText(gIniBackupLabels[idx], smallFont, 105, y + 20, selected ? CARD_ALT : PANEL_BG, WHITE);
        const char *base = strrchr(gIniBackupPaths[idx], '/');
        scene->DrawText((char *)(base ? base + 1 : gIniBackupPaths[idx]), smallFont, 520, y + 20, selected ? CARD_ALT : PANEL_BG, MUTED);
    }
    scene->DrawText(gStatus, smallFont, 75, 902, BG, AMBER);
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "RESTORE"}, {"O", "BACK"}, {"SQUARE", "DELETE"}};
    DrawButtonLegend(scene, smallFont, legend, 4);
}
