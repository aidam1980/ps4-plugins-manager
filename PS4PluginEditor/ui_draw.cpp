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

// Grid thumbnails are decoded once and kept in memory. Decoding PNG files inside
// DrawGamePlugins() caused repeated filesystem I/O and PNG decompression every frame.
struct PluginThumbCacheEntry
{
    char path[512];
    PngImage image;
    bool attempted;
};
static PluginThumbCacheEntry gPluginThumbCache[9] = {};

static const PngImage *GetPluginThumbnail(int index)
{
    if (index < 0 || index >= gPluginCount) return NULL;
    // Only nine cards are visible at once; reuse those slots when changing pages
    // so decoded artwork memory stays bounded.
    PluginThumbCacheEntry &entry = gPluginThumbCache[index % 9];
    const char *pluginPath = gPlugins[index].path;
    if (strcmp(entry.path, pluginPath) != 0)
    {
        FreePngImage(&entry.image);
        memset(&entry, 0, sizeof(entry));
        CopyText(entry.path, sizeof(entry.path), pluginPath);
    }
    if (!entry.attempted)
    {
        entry.attempted = true;
        char imagePath[640];
        char imageName[256];
        const char *pluginBase = strrchr(pluginPath, '/');
        pluginBase = pluginBase ? pluginBase + 1 : pluginPath;
        CopyText(imageName, sizeof(imageName), pluginBase);
        char *dot = strrchr(imageName, '.');
        if (dot) *dot = '\0';
        strncat(imageName, ".png", sizeof(imageName) - strlen(imageName) - 1);

        CopyText(imagePath, sizeof(imagePath), pluginPath);
        dot = strrchr(imagePath, '.');
        if (dot) *dot = '\0';
        strncat(imagePath, ".png", sizeof(imagePath) - strlen(imagePath) - 1);
        if (!LoadPngRGBA(imagePath, &entry.image))
        {
            snprintf(imagePath, sizeof(imagePath), "%s/%s", PLUGIN_ROOT, imageName);
            if (!LoadPngRGBA(imagePath, &entry.image))
            {
                snprintf(imagePath, sizeof(imagePath), "%s/images/%s", PLUGIN_ROOT, imageName);
                LoadPngRGBA(imagePath, &entry.image);
            }
        }
    }
    return entry.image.rgba ? &entry.image : NULL;
}

// =============================================================================
// UI toolkit: rounded shapes, badges and indicators built only from the
// rectangle/text primitives of Scene2D (no alpha blending is available).
// =============================================================================
static int ISqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}

static Color MakeColor(int r, int g, int b)
{
    Color c = {0, 0, 0};
    c.r = (uint8_t)r; c.g = (uint8_t)g; c.b = (uint8_t)b;
    return c;
}

// Text measuring. The console's FreeType build reported no usable glyph metrics, so widths come
// from tables of the real per-character advances of the UI font (Gontserrat-Regular) at the two
// sizes the app uses (27 px and 42 px). No FreeType calls are needed, which also keeps this fast.
static const unsigned char kAdv27[95] = {7,6,9,19,17,26,18,5,8,8,9,13,6,10,6,9,19,10,16,16,16,17,17,15,17,17,6,6,13,13,13,12,26,20,19,19,21,18,15,20,21,8,13,19,14,25,21,22,18,22,20,17,15,21,18,28,18,17,18,9,9,9,14,15,9,16,18,15,18,16,9,17,18,7,7,16,8,28,18,17,18,18,10,12,11,18,14,24,15,14,14,9,7,9,13};
static const unsigned char kAdv42[95] = {11,9,14,30,26,40,28,8,13,13,14,21,9,15,9,14,30,16,24,25,25,26,27,24,27,27,9,9,21,21,21,18,41,30,30,29,33,27,24,32,33,12,21,30,22,39,33,34,28,35,30,26,24,32,28,44,28,26,28,13,14,13,22,24,14,24,28,24,29,25,14,27,28,12,11,24,12,44,28,26,28,29,16,19,17,27,21,37,23,22,22,14,11,14,21};

static FT_Face sLargeFace = NULL, sSmallFace = NULL;

void RegisterUiFonts(FT_Face largeFont, FT_Face smallFont)
{
    sLargeFace = largeFont;
    sSmallFace = smallFont;
}

int TextWidthPx(const char *text, int pixelSize)
{
    if (!text) return 0;
    const bool large = pixelSize >= 36;
    const unsigned char *table = large ? kAdv42 : kAdv27;
    const int fallback = large ? 22 : 14; // non-ASCII bytes
    int width = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        width += (*p >= 32 && *p <= 126) ? table[*p - 32] : fallback;
    return width;
}

int TextWidth(FT_Face font, const char *text)
{
    const bool large = font && font == sLargeFace && sLargeFace != sSmallFace;
    return TextWidthPx(text, large ? 42 : 27);
}

// Trim text with "..." so that it fits a pixel width instead of a guessed character count.
void ShortenToWidthPx(const char *src, char *dst, size_t dstSize, int maxPixels, int pixelSize)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!src) return;
    CopyText(dst, dstSize, src);
    if (TextWidthPx(dst, pixelSize) <= maxPixels) return;
    size_t len = strlen(dst);
    while (len > 1)
    {
        --len;
        // Never cut inside a UTF-8 sequence.
        while (len > 1 && (((unsigned char)dst[len]) & 0xC0) == 0x80) --len;
        dst[len] = '\0';
        char probe[512];
        snprintf(probe, sizeof(probe), "%s...", dst);
        if (TextWidthPx(probe, pixelSize) <= maxPixels)
        {
            CopyText(dst, dstSize, probe);
            return;
        }
    }
}

void ShortenToWidth(FT_Face font, const char *src, char *dst, size_t dstSize, int maxPixels)
{
    const bool large = font && font == sLargeFace && sLargeFace != sSmallFace;
    ShortenToWidthPx(src, dst, dstSize, maxPixels, large ? 42 : 27);
}

void FillRound(Scene2D *scene, int x, int y, int w, int h, int r, Color color)
{
    if (!scene || w <= 0 || h <= 0) return;
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    if (r <= 1)
    {
        scene->DrawRectangle(x, y, w, h, color);
        return;
    }
    scene->DrawRectangle(x, y + r, w, h - 2 * r, color);
    for (int i = 0; i < r; ++i)
    {
        const int d = 2 * (r - i) - 1;
        const int half = ISqrt(4 * r * r - d * d) / 2;
        const int inset = r - half;
        scene->DrawRectangle(x + inset, y + i, w - 2 * inset, 1, color);
        scene->DrawRectangle(x + inset, y + h - 1 - i, w - 2 * inset, 1, color);
    }
}

void DrawCardEdge(Scene2D *scene, int x, int y, int w, int h, int r, Color fill, Color edge)
{
    FillRound(scene, x, y, w, h, r, edge);
    FillRound(scene, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, fill);
}

// Soft lift: thin darker strips under the bottom edge (no full-size overdraw).
void DrawShadow(Scene2D *scene, int x, int y, int w, int h, int r)
{
    if (!scene || w < 2 * r + 4) return;
    scene->DrawRectangle(x + r - 2, y + h, w - 2 * r + 4, 3, SHADOW_SOFT);
    scene->DrawRectangle(x + r, y + h + 3, w - 2 * r, 3, SHADOW_DEEP);
}

void DrawSelectionRing(Scene2D *scene, int x, int y, int w, int h, int r)
{
    FillRound(scene, x - 6, y - 6, w + 12, h + 12, r + 6, ACCENT_GLOW);
}

// Rounded badge. Returns its width so callers can place neighbours.
int DrawPill(Scene2D *scene, FT_Face font, const char *text, int x, int y, int h, Color fg, Color bg)
{
    if (!scene || !text) return 0;
    const int w = TextWidth(font, text) + h;
    FillRound(scene, x, y, w, h, h / 2 < 14 ? h / 2 : 14, bg);
    scene->DrawText((char *)text, font, x + h / 2, y + h / 2 + 9, bg, fg);
    return w;
}

// Check mark drawn with small squares along two diagonals (box = 30 px checkbox at x,y).
void DrawCheckMark(Scene2D *scene, int x, int y, Color color)
{
    for (int i = 0; i < 3; ++i) scene->DrawRectangle(x + 8 + i * 2, y + 15 + i * 2, 4, 4, color);
    for (int i = 0; i < 5; ++i) scene->DrawRectangle(x + 13 + i * 2, y + 19 - i * 2, 4, 4, color);
}

// Folder icon built from rounded shapes (size = width in pixels).
void DrawFolderGlyph(Scene2D *scene, int x, int y, int size, Color color)
{
    const int tabW = size * 2 / 5, tabH = size / 5 + 1;
    FillRound(scene, x, y, tabW, tabH + 2, 3, color);
    FillRound(scene, x, y + tabH - 1, size, size * 2 / 3 + 1, 4, color);
}

// Document icon: outlined page with a few text lines.
void DrawFileGlyph(Scene2D *scene, int x, int y, int size, Color color, Color bg)
{
    const int w = size * 3 / 4;
    const int px = x + (size - w) / 2;
    FillRound(scene, px, y, w, size, 4, color);
    FillRound(scene, px + 2, y + 2, w - 4, size - 4, 3, bg);
    const int lineX = px + 6, lineW = w - 12;
    scene->DrawRectangle(lineX, y + size / 3, lineW, 2, color);
    scene->DrawRectangle(lineX, y + size / 3 + 6, lineW, 2, color);
    scene->DrawRectangle(lineX, y + size / 3 + 12, lineW * 2 / 3, 2, color);
}

// Keep the END of a long path ("...ldHEN/plugins/Mods"), preferring to cut at a slash.
void ShortenPathToWidth(const char *path, char *dst, size_t dstSize, int maxPixels)
{
    if (!dst || dstSize == 0) return;
    dst[0] = '\0';
    if (!path) return;
    if (TextWidthPx(path, 27) <= maxPixels)
    {
        CopyText(dst, dstSize, path);
        return;
    }
    const size_t len = strlen(path);
    for (size_t start = 1; start < len; ++start)
    {
        if (path[start] != '/') continue;
        char probe[600];
        snprintf(probe, sizeof(probe), "...%s", path + start);
        if (TextWidthPx(probe, 27) <= maxPixels)
        {
            CopyText(dst, dstSize, probe);
            return;
        }
    }
    // A single very long name: keep as much of its tail as fits.
    for (size_t start = 1; start < len; ++start)
    {
        char probe[600];
        snprintf(probe, sizeof(probe), "...%s", path + start);
        if (TextWidthPx(probe, 27) <= maxPixels)
        {
            CopyText(dst, dstSize, probe);
            return;
        }
    }
    CopyText(dst, dstSize, "...");
}

void DrawScrollbar(Scene2D *scene, int x, int y, int h, int total, int visible, int first)
{
    if (!scene || total <= visible || visible <= 0 || h < 40) return;
    FillRound(scene, x, y, 6, h, 3, MakeColor(33, 42, 56));
    int thumbH = h * visible / total;
    if (thumbH < 40) thumbH = 40;
    const int travel = h - thumbH;
    const int maxFirst = total - visible;
    if (first < 0) first = 0;
    if (first > maxFirst) first = maxFirst;
    const int thumbY = y + (maxFirst > 0 ? travel * first / maxFirst : 0);
    FillRound(scene, x, thumbY, 6, thumbH, 3, MUTED);
}

void DrawHeading(Scene2D *scene, FT_Face font, const char *text, int baselineY)
{
    if (!scene || !text) return;
    FillRound(scene, 56, baselineY - 23, 5, 29, 2, ACCENT);
    scene->DrawText((char *)text, font, 76, baselineY, BG, WHITE);
}

void DrawStatusLine(Scene2D *scene, FT_Face font, int baselineY, Color color, bool alignRight)
{
    if (!scene || !gStatus[0]) return;
    char shown[260];
    ShortenToWidth(font, gStatus, shown, sizeof(shown), alignRight ? 1100 : 1500);
    const int tw = TextWidth(font, shown);
    const int dotX = alignRight ? 1865 - tw - 22 : 58;
    FillRound(scene, dotX, baselineY - 15, 10, 10, 5, color);
    scene->DrawText(shown, font, dotX + 22, baselineY, BG, color);
}

static void DrawPageIndicator(Scene2D *scene, FT_Face font, int page, int pages)
{
    if (pages < 2) return;
    char text[64];
    snprintf(text, sizeof(text), "PAGE %d / %d", page + 1, pages);
    scene->DrawText(text, font, 58, 898, BG, MUTED);
    if (pages > 14) return;
    const int dotW = 10, activeW = 30, gap = 12;
    const int total = (pages - 1) * (dotW + gap) + activeW;
    int x = SCREEN_W / 2 - total / 2;
    for (int i = 0; i < pages; ++i)
    {
        const bool active = i == page;
        FillRound(scene, x, 888, active ? activeW : dotW, 10, 5, active ? ACCENT : MUTED);
        x += (active ? activeW : dotW) + gap;
    }
}

void DrawImage(Scene2D *scene, const PngImage &image, int x, int y, int w, int h, Color background);

// =============================================================================
// Application chrome
// =============================================================================
void DrawTopBar(Scene2D *scene, FT_Face largeFont, FT_Face smallFont, const char *title, const char *subtitle)
{
    if (!scene) return;
    (void)largeFont;
    // Subtle vertical gradient instead of a flat slab.
    for (int i = 0; i < 54; ++i)
        scene->DrawRectangle(0, i, SCREEN_W, 1, MakeColor(28 - i * 8 / 53, 36 - i * 10 / 53, 50 - i * 14 / 53));
    scene->DrawRectangle(0, 53, SCREEN_W, 1, ACCENT);

    // Small plug-shaped logo mark.
    FillRound(scene, 28, 12, 30, 30, 8, ACCENT);
    FillRound(scene, 33, 17, 20, 20, 5, HEADER);
    scene->DrawRectangle(38, 21, 3, 9, ACCENT);
    scene->DrawRectangle(45, 21, 3, 9, ACCENT);
    scene->DrawRectangle(38, 29, 10, 3, ACCENT);

    if (title) scene->DrawText((char *)title, smallFont, 74, 36, HEADER, WHITE);

    // Right side: screen breadcrumb.
    if (subtitle)
    {
        char shown[200];
        ShortenToWidth(smallFont, subtitle, shown, sizeof(shown), SCREEN_W - 48 - 520);
        scene->DrawText(shown, smallFont, SCREEN_W - 48 - TextWidth(smallFont, shown), 36, HEADER, MUTED);
    }
}

void DrawPanel(Scene2D *scene, int x, int y, int w, int h, Color fill)
{
    if (!scene || w < 5 || h < 5) return;
    int r = h / 4 < 10 ? h / 4 : 10;
    if (r < 2) r = 2;
    if (h >= 40) DrawShadow(scene, x, y, w, h, r);
    DrawCardEdge(scene, x, y, w, h, r, fill, PANEL_EDGE);
}

void DrawFooterKey(Scene2D *scene, FT_Face smallFont, const char *label,
                   int x, int y, int w, int h)
{
    DrawCardEdge(scene, x, y, w, h, 8, KEY_BG, PANEL_EDGE);
    scene->DrawText((char *)label, smallFont, x + (w - TextWidth(smallFont, label)) / 2, y + h / 2 + 9, KEY_BG, WHITE);
}

void DrawFooter(Scene2D *scene, FT_Face smallFont, const char *controls)
{
    const int panelY = 934, keyY = 954, textY = 991;
    DrawPanel(scene, 48, panelY, 1060, 98, PANEL_BG);
    if (controls && strstr(controls, "UP/DOWN"))
    {
        DrawFooterKey(scene, smallFont, "UP/DN", 72, keyY, 94, 58);
        scene->DrawText((char *)"SELECT FILE", smallFont, 179, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(328, 955, 1, 55, DIVIDER);
        DrawFooterKey(scene, smallFont, "X", 351, keyY, 49, 58);
        scene->DrawText((char *)"TOGGLE INI", smallFont, 414, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(565, 955, 1, 55, DIVIDER);
        DrawFooterKey(scene, smallFont, "O", 608, keyY, 49, 58);
        scene->DrawText((char *)"BACK", smallFont, 669, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(756, 955, 1, 55, DIVIDER);
        DrawFooterKey(scene, smallFont, "SQR", 778, keyY, 57, 58);
        scene->DrawText((char *)"RESCAN", smallFont, 846, textY, PANEL_BG, WHITE);
    }
    else
    {
        DrawFooterKey(scene, smallFont, "ARROWS", 72, keyY, 108, 58);
        scene->DrawText((char *)"MOVE", smallFont, 195, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(294, 955, 1, 55, DIVIDER);
        DrawFooterKey(scene, smallFont, "X", 319, keyY, 49, 58);
        scene->DrawText((char *)"OPEN GAME", smallFont, 382, textY, PANEL_BG, WHITE);
        scene->DrawRectangle(566, 955, 1, 55, DIVIDER);
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
    if (gSearchQuery[0])
    {
        const LegendButton legend[] = {{"X", "SELECT PLUGIN"}, {"TRIANGLE", "SELECTED PLUGINS"}, {"SQUARE", "SEARCH"}, {"O", "CLEAR SEARCH"}, {"L1R1", "SWITCH LIST"}};
        DrawButtonLegend(scene, font, legend, 5);
    }
    else
    {
        const LegendButton legend[] = {{"X", "SELECT PLUGIN"}, {"TRIANGLE", "SHOW SELECTED PLUGINS"}, {"SQUARE", "SEARCH"}, {"L1R1", "SWITCH LIST"}};
        DrawButtonLegend(scene, font, legend, 4);
    }
}

bool LoadButtonAsset(const char *name, PngImage *image);
void DrawButtonLegend(Scene2D *scene, FT_Face font, const LegendButton *items, int count);

void DrawNavFooter(Scene2D *scene, FT_Face font, const char *controls)
{
    if (controls && strstr(controls, "TOGGLE PLUGIN"))
    {
        const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "TOGGLE PLUGIN"}, {"O", "BACK"}};
        DrawButtonLegend(scene, font, legend, 3);
        return;
    }
    if (controls && strstr(controls, "TOGGLE GAME"))
    {
        const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "TOGGLE GAME"}, {"TRIANGLE", "APPLY"}, {"O", "BACK"}};
        DrawButtonLegend(scene, font, legend, 4);
        return;
    }
    if (controls && strstr(controls, "BACKSPACE"))
    {
        const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "INSERT"}, {"SQUARE", "BACKSPACE"}, {"TRIANGLE", "SAVE"}, {"O", "CANCEL"}};
        DrawButtonLegend(scene, font, legend, 5);
        return;
    }
    DrawPanel(scene, 48, 934, 1824, 98, PANEL_BG);
}

// Controller-button legend: icon above its action, groups separated by hairlines.
void DrawButtonLegend(Scene2D *scene, FT_Face font, const LegendButton *items, int count)
{
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
    const int iconY = 938;
    for (int i = 0; i < count; ++i)
    {
        const int x = startX + i * (itemW + gap);
        if (i > 0) scene->DrawRectangle(x - gap / 2, 952, 1, 62, DIVIDER);
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
            DrawImage(scene, l1, x + itemW / 2 - 58, iconY + 6, 54, 34, PANEL_BG);
            DrawImage(scene, r1, x + itemW / 2 + 4, iconY + 6, 54, 34, PANEL_BG);
        }
        else if (asset && asset->rgba)
        {
            int iconW = 50, iconH = 50;
            if (!strcmp(b, "L1") || !strcmp(b, "R1")) { iconW = 72; iconH = 46; }
            else if (!strcmp(b, "L2") || !strcmp(b, "R2")) { iconW = 62; iconH = 42; }
            else if (!strcmp(b, "OPTIONS")) { iconW = 44; iconH = 44; }
            else if (!strcmp(b, "ARROWS")) { iconW = 48; iconH = 48; }
            DrawImage(scene, *asset, x + (itemW - iconW) / 2, iconY + (52 - iconH) / 2, iconW, iconH, PANEL_BG);
        }
        // Measure the real text width (FreeType) so each label is truly centred.
        char label[96];
        ShortenToWidth(font, items[i].action, label, sizeof(label), itemW + 4);
        scene->DrawText(label, font, x + (itemW - TextWidth(font, label)) / 2, 1019, PANEL_BG, WHITE);
    }
}

void DrawAppChrome(Scene2D *scene, FT_Face largeFont, FT_Face smallFont, const char *subtitle)
{
    scene->FrameBufferFill(BG);
    DrawTopBar(scene, largeFont, smallFont, "Plugin Manager", subtitle);
    const char *tabs[3] = {"MY GAMES", "GAME PLUGINS", "plugins.ini"};
    const int xs[3] = {62, 670, 1278};
    for (int i = 0; i < 3; ++i)
    {
        const bool active = gTab == i;
        Color fill = active ? CARD_ALT : PANEL_BG;
        DrawCardEdge(scene, xs[i], 66, 580, 52, 10, fill, active ? ACCENT : PANEL_EDGE);
        const int tw = TextWidth(smallFont, tabs[i]);
        scene->DrawText((char *)tabs[i], smallFont, xs[i] + (580 - tw) / 2, 101, fill, active ? WHITE : MUTED);
        if (active) scene->DrawRectangle(xs[i] + 20, 112, 540, 3, ACCENT);
    }
}

// =============================================================================
// Shared list/table helpers
// =============================================================================
// Table geometry shared by the plugin list, per-game list and backup history.
static const int TBL_Y = 178, TBL_H = 700, TBL_ROW0 = 232, TBL_PITCH = 68, TBL_ROW_H = 58, TBL_VISIBLE = 9;

static void DrawTableShell(Scene2D *scene, FT_Face font, const char *col1, const char *col2, int col2X)
{
    DrawPanel(scene, 55, TBL_Y, 1810, TBL_H, PANEL_BG);
    scene->DrawRectangle(66, TBL_Y + 2, 1788, 40, ROW_ALT);
    scene->DrawText((char *)col1, font, 122, TBL_Y + 31, ROW_ALT, MUTED);
    if (col2) scene->DrawText((char *)col2, font, col2X, TBL_Y + 31, ROW_ALT, MUTED);
    scene->DrawRectangle(66, TBL_Y + 42, 1788, 1, DIVIDER);
}

// One plugin row: status dot, file name and a state badge.
static void DrawPluginRow(Scene2D *scene, FT_Face font, int rowTop, int index, bool selected,
                          const char *label, const char *badge, Color badgeFg, Color badgeBg, Color dot)
{
    const int rx = 68, rw = 1772, rh = TBL_ROW_H;
    Color rowBg = selected ? CARD_ALT : ((index & 1) ? ROW_ALT : PANEL_BG);
    if (selected) DrawCardEdge(scene, rx, rowTop, rw, rh, 8, rowBg, ACCENT);
    else if (index & 1) FillRound(scene, rx, rowTop, rw, rh, 8, rowBg);
    FillRound(scene, 92, rowTop + rh / 2 - 6, 12, 12, 6, dot);
    char shown[300];
    ShortenToWidth(font, label, shown, sizeof(shown), 1300);
    scene->DrawText(shown, font, 122, rowTop + rh / 2 + 9, rowBg, selected ? WHITE : MUTED);
    DrawPill(scene, font, badge, 1517, rowTop + (rh - 38) / 2, 38, badgeFg, badgeBg);
}

// Placeholder for plugins without artwork: a plug glyph, centred in the box.
void DrawPlugGlyph(Scene2D *scene, int cx, int cy, int size, Color color, Color bg)
{
    const int u = size / 10;
    FillRound(scene, cx - 5 * u, cy - 5 * u, 10 * u, 10 * u, 2 * u, color);
    FillRound(scene, cx - 4 * u + 1, cy - 4 * u + 1, 8 * u - 2, 8 * u - 2, 2 * u - 1, bg);
    scene->DrawRectangle(cx - 2 * u, cy - 3 * u, u, 3 * u, color);
    scene->DrawRectangle(cx + u, cy - 3 * u, u, 3 * u, color);
    scene->DrawRectangle(cx - 2 * u, cy, 4 * u, u, color);
    scene->DrawRectangle(cx - u / 2, cy + u, u, 2 * u, color);
}

void DrawGameGrid(Scene2D *scene, FT_Face smallFont, bool multiSelect)
{
    // My Games shows the searched (filtered) list; the plugin assignment grid shows every game.
    const bool filtered = !multiSelect;
    const int total = filtered ? gFilteredCount : gGameCount;
    int selectedPos = filtered ? GameFilterPosition(gSelectedGame) : gSelectedGame;
    if (selectedPos < 0) selectedPos = 0;
    const int pageStart = (selectedPos / GAME_PAGE_SIZE) * GAME_PAGE_SIZE;
    int pageIndices[GAME_PAGE_SIZE];
    for (int slot = 0; slot < GAME_PAGE_SIZE; ++slot)
    {
        const int pos = pageStart + slot;
        pageIndices[slot] = pos < total ? (filtered ? gFilteredGames[pos] : pos) : -1;
    }
    // While a direction is auto-repeating, don't decode a new page of icons every few steps.
    bool iconsReady = true;
    if (gNavFast && !PageIconsReady(pageIndices)) iconsReady = false;
    else LoadPageIconsByIndex(pageIndices);
    const int cardW = 590, cardH = 220, startX = 55, startY = 174;
    const int gapX = 20, gapY = 12;
    for (int slot = 0; slot < GAME_PAGE_SIZE; ++slot)
    {
        int index = pageIndices[slot];
        if (index < 0) break;
        int x = startX + (slot % 3) * (cardW + gapX);
        int y = startY + (slot / 3) * (cardH + gapY);
        bool selected = index == gSelectedGame;
        Color fill = selected ? CARD_ALT : CARD;
        DrawShadow(scene, x, y, cardW, cardH, 12);
        if (selected) DrawSelectionRing(scene, x, y, cardW, cardH, 12);
        DrawCardEdge(scene, x, y, cardW, cardH, 12, fill, selected ? ACCENT : PANEL_EDGE);
        if (multiSelect && gBulkSelected[index] && !selected)
            scene->DrawRectangle(x + 1, y + 24, 4, cardH - 48, GREEN);

        PngImage &icon = gPageIcons[slot];
        const int iconBox = 188;
        const int iconX = x + 22, iconY = y + (cardH - iconBox) / 2;
        scene->DrawRectangle(iconX - 2, iconY - 2, iconBox + 4, iconBox + 4, selected ? ACCENT_DIM : PANEL_EDGE);
        scene->DrawRectangle(iconX, iconY, iconBox, iconBox, HEADER);
        if (!iconsReady)
        {
            // icons appear as soon as the cursor settles
        }
        else if (icon.rgba && icon.width > 0 && icon.height > 0)
        {
            float scale = (float)iconBox / (float)(icon.width > icon.height ? icon.width : icon.height);
            int drawW = (int)(icon.width * scale), drawH = (int)(icon.height * scale);
            DrawImage(scene, icon, iconX + (iconBox - drawW) / 2, iconY + (iconBox - drawH) / 2,
                      drawW, drawH, HEADER);
        }
        else
        {
            const int tw = TextWidth(smallFont, "NO ICON");
            scene->DrawText((char *)"NO ICON", smallFont, iconX + (iconBox - tw) / 2, iconY + iconBox / 2 + 9, HEADER, MUTED);
        }

        const int textX = x + 232, textW = cardW - 232 - 22;
        char displayTitle[160];
        ShortenToWidth(smallFont, gGames[index].title, displayTitle, sizeof(displayTitle), textW);
        scene->DrawText(displayTitle, smallFont, textX, y + 70, fill, WHITE);
        if (strcasecmp(gGames[index].title, gGames[index].id) != 0)
            scene->DrawText(gGames[index].id, smallFont, textX, y + 104, fill, MUTED);

        if (multiSelect && gBulkSelected[index])
        {
            FillRound(scene, textX, y + 126, 12, 12, 6, GREEN);
            scene->DrawText((char *)"Already selected", smallFont, textX + 22, y + 138, fill, GREEN);
        }
        const int pillY = y + cardH - 26 - 38;
        if (!multiSelect)
        {
            DrawPill(scene, smallFont, "Select Plugin", textX, pillY, 38,
                     selected ? ACCENT : MUTED, selected ? ACCENT_DIM : NEUTRAL_DIM);
        }
        else if (!gBulkSelected[index])
        {
            DrawPill(scene, smallFont, "NOT SELECTED", textX, pillY, 38,
                     selected ? ACCENT : MUTED, selected ? ACCENT_DIM : NEUTRAL_DIM);
        }
    }
    DrawPageIndicator(scene, smallFont, pageStart / GAME_PAGE_SIZE,
                      (total + GAME_PAGE_SIZE - 1) / GAME_PAGE_SIZE);
}

void DrawMyGames(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont,
                  gInDetails && gGameCount > 0 ? "MY GAMES  /  GAME PLUGINS" : "PS4 PLUGIN MANAGER");
    if (!gInDetails)
    {
        DrawHeading(scene, smallFont, "MY GAMES", 150);
        char countText[64];
        if (gSearchQuery[0]) snprintf(countText, sizeof(countText), "%d of %d games", gFilteredCount, gGameCount);
        else snprintf(countText, sizeof(countText), "%d found", gGameCount);
        const int pillW = DrawPill(scene, smallFont, countText, 76 + TextWidth(smallFont, "MY GAMES") + 20, 126, 34,
                                   ACCENT, ACCENT_DIM);
        if (gSearchQuery[0])
        {
            char query[64], chip[96];
            ShortenToWidth(smallFont, gSearchQuery, query, sizeof(query), 420);
            snprintf(chip, sizeof(chip), "SEARCH: %s", query);
            DrawPill(scene, smallFont, chip, 76 + TextWidth(smallFont, "MY GAMES") + 20 + pillW + 12, 126, 34,
                     AMBER, AMBER_DIM);
        }
        if (gFilteredCount > 0) DrawGameGrid(scene, smallFont, false);
        else if (gGameCount > 0)
        {
            DrawPanel(scene, 360, 320, 1200, 300, PANEL_BG);
            const char *title = "No games match your search";
            scene->DrawText((char *)title, largeFont, 960 - TextWidthPx(title, 42) / 2, 420, PANEL_BG, WHITE);
            char query[64], line[120];
            ShortenToWidth(smallFont, gSearchQuery, query, sizeof(query), 700);
            snprintf(line, sizeof(line), "\"%s\"", query);
            scene->DrawText(line, smallFont, 960 - TextWidth(smallFont, line) / 2, 480, PANEL_BG, AMBER);
            const char *hint = "SQUARE: change search     CIRCLE: clear search";
            scene->DrawText((char *)hint, smallFont, 960 - TextWidth(smallFont, hint) / 2, 550, PANEL_BG, MUTED);
        }
        else
        {
            DrawPanel(scene, 360, 320, 1200, 300, PANEL_BG);
            const char *title = "No installed game folders found";
            scene->DrawText((char *)title, largeFont, 960 - TextWidthPx(title, 42) / 2, 420, PANEL_BG, WHITE);
            scene->DrawText((char *)"SCAN STATUS", smallFont, 420, 485, PANEL_BG, MUTED);
            char shown[260];
            ShortenToWidth(smallFont, gStatus, shown, sizeof(shown), 1080);
            scene->DrawText(shown, smallFont, 420, 535, PANEL_BG, AMBER);
        }
        DrawMyGamesFooter(scene, smallFont);
        if (gShowSelectedPlugins && gFilteredCount > 0)
        {
            const Game &selectedGame = gGames[gSelectedGame];
            LoadConfigEntries();
            DrawPanel(scene, 510, 300, 900, 500, PANEL_BG);
            scene->DrawRectangle(530, 301, 860, 4, ACCENT);
            scene->DrawText((char *)"ENABLED PLUGINS", largeFont, 570, 365, PANEL_BG, WHITE);
            char gameLine[200];
            ShortenToWidth(smallFont, selectedGame.title, gameLine, sizeof(gameLine), 780);
            scene->DrawText(gameLine, smallFont, 572, 405, PANEL_BG, MUTED);
            scene->DrawRectangle(560, 425, 800, 1, DIVIDER);
            int lineY = 470, count = 0;
            for (int ci = 0; ci < gConfigCount && count < 8; ++ci)
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
                FillRound(scene, 574, lineY - 12, 12, 12, 6, GREEN);
                char shownBase[200];
                ShortenToWidth(smallFont, base, shownBase, sizeof(shownBase), 740);
                scene->DrawText(shownBase, smallFont, 604, lineY, PANEL_BG, WHITE);
                lineY += 38; ++count;
            }
            if (count == 0)
                scene->DrawText((char *)"No enabled plugins in this game's section.", smallFont, 572, lineY, PANEL_BG, MUTED);
            DrawPill(scene, smallFont, "CIRCLE: CLOSE", 570, 745, 40, WHITE, NEUTRAL_DIM);
        }
        return;
    }

    const Game &game = gGames[gSelectedGame];
    char heading[140];
    snprintf(heading, sizeof(heading), "%s  /  PLUGINS", game.id);
    DrawHeading(scene, smallFont, heading, 150);
    DrawTableShell(scene, smallFont, "PLUGIN FILE", "STATE FOR THIS GAME", 1535);
    const int visible = TBL_VISIBLE;
    if (gSelectedPlugin < gPluginScroll) gPluginScroll = gSelectedPlugin;
    if (gSelectedPlugin >= gPluginScroll + visible) gPluginScroll = gSelectedPlugin - visible + 1;
    if (gPluginScroll < 0) gPluginScroll = 0;
    if (gPluginCount == 0)
        scene->DrawText((char *)"No .prx/.sprx files found in /data/GoldHEN/plugins", smallFont, 122, 300, PANEL_BG, MUTED);
    for (int row = 0; row < visible; ++row)
    {
        int idx = gPluginScroll + row;
        if (idx >= gPluginCount) break;
        int y = TBL_ROW0 + row * TBL_PITCH;
        const char *pluginName = strrchr(gPlugins[idx].path, '/');
        pluginName = pluginName ? pluginName + 1 : gPlugins[idx].path;
        const bool on = gPlugins[idx].enabled;
        DrawPluginRow(scene, smallFont, y, idx, idx == gSelectedPlugin, pluginName,
                      on ? "ENABLED" : "DISABLED", on ? GREEN : MUTED, on ? GREEN_DIM : NEUTRAL_DIM,
                      on ? GREEN : PANEL_EDGE);
    }
    DrawScrollbar(scene, 1851, TBL_ROW0, TBL_VISIBLE * TBL_PITCH - 10, gPluginCount, visible, gPluginScroll);
    DrawStatusLine(scene, smallFont, 908, AMBER);
    const LegendButton legend[] = {{"ARROWS", "SELECT FILE"}, {"X", "TOGGLE"}, {"O", "BACK"}, {"L1", "PREV LIST"}, {"R1", "NEXT LIST"}};
    DrawButtonLegend(scene, smallFont, legend, 5);
}

// "USE NUMBER" cell: DEFAULT when enabled in [default], otherwise the number of games using it.
static const char *PluginUseLabel(const PluginEntry &p, char *buf, size_t bufSize,
                                  Color *fg, Color *bg, Color *dot)
{
    bool defaultOn = false;
    const int games = CountPluginGames(p.path, &defaultOn);
    if (defaultOn)
    {
        *fg = GREEN; *bg = GREEN_DIM; *dot = GREEN;
        snprintf(buf, bufSize, "DEFAULT");
    }
    else if (games > 0)
    {
        *fg = ACCENT; *bg = ACCENT_DIM; *dot = ACCENT;
        snprintf(buf, bufSize, "%d GAME%s", games, games == 1 ? "" : "S");
    }
    else
    {
        *fg = MUTED; *bg = NEUTRAL_DIM; *dot = PANEL_EDGE;
        snprintf(buf, bufSize, "0 GAMES");
    }
    return buf;
}

void DrawGamePlugins(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "PLUGIN FILES  /  ASSIGN TO ONE OR MORE GAMES");
    if (gBulkView == 2)
    {
        DrawHeading(scene, smallFont, "PLUGIN STATUS BY GAME", 150);
        char countText[64];
        snprintf(countText, sizeof(countText), "ENABLED IN %d GAMES", gBulkSelectedCount);
        DrawPill(scene, smallFont, countText, 76 + TextWidth(smallFont, "PLUGIN STATUS BY GAME") + 20, 126, 34,
                 gBulkSelectedCount > 0 ? GREEN : MUTED, gBulkSelectedCount > 0 ? GREEN_DIM : NEUTRAL_DIM);
        if (gSelectedPlugin >= 0 && gSelectedPlugin < gPluginCount)
        {
            const char *base = strrchr(gPlugins[gSelectedPlugin].path, '/');
            base = base ? base + 1 : gPlugins[gSelectedPlugin].path;
            char shown[200];
            ShortenToWidth(smallFont, base, shown, sizeof(shown), 640);
            scene->DrawText(shown, smallFont, 1865 - TextWidth(smallFont, shown), 150, BG, MUTED);
        }
        if (gGameCount > 0) DrawGameGrid(scene, smallFont, true);
        else scene->DrawText((char *)"No games found; cannot assign this plugin.", largeFont, 200, 420, BG, AMBER);
        DrawNavFooter(scene, smallFont, "ARROWS MOVE   X TOGGLE PLUGIN FOR THIS GAME   O BACK");
        return;
    }

    DrawHeading(scene, smallFont, "MY PLUGINS", 150);
    {
        const int hx = 76 + TextWidth(smallFont, "MY PLUGINS") + 20;
        const int w = DrawPill(scene, smallFont, gPluginGridView ? "GRID VIEW" : "LIST VIEW", hx, 126, 34, ACCENT, ACCENT_DIM);
        char countText[48];
        snprintf(countText, sizeof(countText), "%d plugin%s", gPluginCount, gPluginCount == 1 ? "" : "s");
        DrawPill(scene, smallFont, countText, hx + w + 12, 126, 34, MUTED, NEUTRAL_DIM);
    }
    if (gPluginGridView)
    {
        const int perPage = 9;
        int pageStart = (gSelectedPlugin / perPage) * perPage;
        const int cardW = 590, cardH = 220, startX = 55, startY = 174, gapX = 20, gapY = 12;
        for (int slot = 0; slot < perPage; ++slot)
        {
            int idx = pageStart + slot;
            if (idx >= gPluginCount) break;
            int x = startX + (slot % 3) * (cardW + gapX);
            int y = startY + (slot / 3) * (cardH + gapY);
            bool selected = idx == gSelectedPlugin;
            Color fill = selected ? CARD_ALT : CARD;
            DrawShadow(scene, x, y, cardW, cardH, 12);
            if (selected) DrawSelectionRing(scene, x, y, cardW, cardH, 12);
            DrawCardEdge(scene, x, y, cardW, cardH, 12, fill, selected ? ACCENT : PANEL_EDGE);

            // Decoded once per plugin and cached; no I/O in the render loop.
            const PngImage *image = GetPluginThumbnail(idx);
            const int box = 176, ix = x + 22, iy = y + (cardH - box) / 2;
            scene->DrawRectangle(ix - 2, iy - 2, box + 4, box + 4, selected ? ACCENT_DIM : PANEL_EDGE);
            scene->DrawRectangle(ix, iy, box, box, HEADER);
            if (image && image->rgba && image->width > 0 && image->height > 0)
            {
                float scale = (float)box / (float)(image->width > image->height ? image->width : image->height);
                int dw = (int)(image->width * scale), dh = (int)(image->height * scale);
                DrawImage(scene, *image, ix + (box - dw) / 2, iy + (box - dh) / 2, dw, dh, HEADER);
            }
            else
            {
                DrawPlugGlyph(scene, ix + box / 2, iy + box / 2, 70, selected ? ACCENT_DIM : NEUTRAL_DIM, HEADER);
            }
            const char *base = strrchr(gPlugins[idx].path, '/');
            base = base ? base + 1 : gPlugins[idx].path;
            char shown[200];
            ShortenToWidth(smallFont, base, shown, sizeof(shown), cardW - 224 - 20);
            scene->DrawText(shown, smallFont, x + 224, y + 90, fill, selected ? WHITE : MUTED);
            Color fg, bg, dot;
            char useText[32];
            const char *state = PluginUseLabel(gPlugins[idx], useText, sizeof(useText), &fg, &bg, &dot);
            DrawPill(scene, smallFont, state, x + 224, y + 116, 38, fg, bg);
        }
        DrawPageIndicator(scene, smallFont, pageStart / perPage, (gPluginCount + perPage - 1) / perPage);
        DrawStatusLine(scene, smallFont, 908, AMBER, true);
        const LegendButton legend[] = {{"ARROWS", "SELECT PLUGIN"}, {"X", "ASSIGN"}, {"TRIANGLE", "GET"}, {"OPTIONS", "SET DEFAULT"}, {"R3", "LIST / GRID"}, {"SQUARE", "REFRESH"}, {"L1R1", "SWITCH LIST"}};
        DrawButtonLegend(scene, smallFont, legend, 7);
        return;
    }

    DrawTableShell(scene, smallFont, "PLUGIN FILE", "USE NUMBER", 1535);
    const int visible = TBL_VISIBLE;
    if (gSelectedPlugin < gPluginScroll) gPluginScroll = gSelectedPlugin;
    if (gSelectedPlugin >= gPluginScroll + visible) gPluginScroll = gSelectedPlugin - visible + 1;
    if (gPluginScroll < 0) gPluginScroll = 0;
    if (gPluginCount == 0)
        scene->DrawText((char *)"No .prx/.sprx files found in this folder.", smallFont, 122, 300, PANEL_BG, MUTED);
    for (int row = 0; row < visible; ++row)
    {
        int idx = gPluginScroll + row;
        if (idx >= gPluginCount) break;
        int y = TBL_ROW0 + row * TBL_PITCH;
        const char *fileName = strrchr(gPlugins[idx].path, '/');
        fileName = fileName ? fileName + 1 : gPlugins[idx].path;
        Color fg, bg, dot;
        char useText[32];
        const char *state = PluginUseLabel(gPlugins[idx], useText, sizeof(useText), &fg, &bg, &dot);
        DrawPluginRow(scene, smallFont, y, idx, idx == gSelectedPlugin, fileName, state, fg, bg, dot);
    }
    DrawScrollbar(scene, 1851, TBL_ROW0, TBL_VISIBLE * TBL_PITCH - 10, gPluginCount, visible, gPluginScroll);
    DrawStatusLine(scene, smallFont, 908, AMBER);
    const LegendButton legend[] = {{"ARROWS", "SELECT"}, {"X", "ASSIGN"}, {"TRIANGLE", "GET"}, {"OPTIONS", "SET DEFAULT"}, {"R3", "LIST / GRID"}, {"SQUARE", "REFRESH"}, {"L1R1", "SWITCH LIST"}};
    DrawButtonLegend(scene, smallFont, legend, 7);
}

void DrawPluginsIni(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "LIVE VIEW  /  /data/GoldHEN/plugins.ini");
    DrawHeading(scene, smallFont, "PLUGINS.INI", 150);
    char countText[48];
    snprintf(countText, sizeof(countText), "%d lines", gConfigLineCount);
    DrawPill(scene, smallFont, countText, 76 + TextWidth(smallFont, "PLUGINS.INI") + 20, 126, 34, ACCENT, ACCENT_DIM);

    // Colour key at fixed positions (no measuring needed).
    {
        struct Key { const char *label; Color color; int x; };
        const Key keys[3] = {{"GAME SECTION", GREEN, 1240}, {"FILE FOUND", WHITE, 1500}, {"MISSING", RED, 1715}};
        for (int i = 0; i < 3; ++i)
        {
            FillRound(scene, keys[i].x, 137, 12, 12, 6, keys[i].color);
            scene->DrawText((char *)keys[i].label, smallFont, keys[i].x + 22, 150, BG, MUTED);
        }
    }

    DrawPanel(scene, 55, TBL_Y, 1810, TBL_H, PANEL_BG);
    if (gConfigLineCount == 0)
    {
        scene->DrawText((char *)"File is missing, empty, or unreadable.", smallFont, 95, 250, PANEL_BG, AMBER);
        scene->DrawText((char *)PLUGINS_INI, smallFont, 95, 310, PANEL_BG, MUTED);
    }
    const int visible = 10;
    if (gIniCursor < gIniScroll) gIniScroll = gIniCursor;
    if (gIniCursor >= gIniScroll + visible) gIniScroll = gIniCursor - visible + 1;
    if (gIniScroll < 0) gIniScroll = 0;
    for (int row = 0; row < visible; ++row)
    {
        int idx = gIniScroll + row;
        if (idx >= gConfigLineCount) break;
        int y = 200 + row * 64;
        char line[CONFIG_LINE_SIZE];
        CopyText(line, sizeof(line), gConfigLines[idx]);
        Trim(line);
        bool selected = idx == gIniCursor;
        Color rowBg = selected ? CARD_ALT : PANEL_BG;
        if (selected) DrawCardEdge(scene, 68, y, 1772, 54, 8, rowBg, ACCENT);
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
        char numbered[16];
        snprintf(numbered, sizeof(numbered), "%04d", idx + 1);
        scene->DrawText(numbered, smallFont, 92, y + 36, rowBg, selected ? ACCENT : MUTED);
        scene->DrawRectangle(176, y + 8, 1, 38, selected ? ACCENT_DIM : DIVIDER);
        char shown[CONFIG_LINE_SIZE];
        ShortenToWidth(smallFont, line, shown, sizeof(shown), 1620);
        scene->DrawText(shown, smallFont, 196, y + 36, rowBg, textColor);
    }
    DrawScrollbar(scene, 1851, 200, 10 * 64 - 10, gConfigLineCount, visible, gIniScroll);
    DrawStatusLine(scene, smallFont, 908, MUTED);
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "EDIT"}, {"SQUARE", "DELETE"}, {"L2", "COPY"}, {"R2", "PASTE"}, {"TRIANGLE", "BACKUP"}, {"OPTIONS", "HISTORY"}};
    DrawButtonLegend(scene, smallFont, legend, 7);
}

// On-screen keyboard shared by the plugins.ini editor and the My Games search.
static void DrawKeyboardScreen(Scene2D *scene, FT_Face largeFont, FT_Face smallFont,
                               const char *subtitle, const char *label, const char *buffer,
                               int cursor, const char *rightNote,
                               const LegendButton *legend, int legendCount)
{
    DrawAppChrome(scene, largeFont, smallFont, subtitle);
    DrawPanel(scene, 70, 245, 1780, 640, PANEL_BG);
    scene->DrawText((char *)label, smallFont, 100, 292, PANEL_BG, ACCENT);
    if (rightNote && rightNote[0])
        scene->DrawText((char *)rightNote, smallFont, 1824 - TextWidth(smallFont, rightNote), 292, PANEL_BG, MUTED);

    // Text field with a caret at the end of the buffer.
    DrawCardEdge(scene, 96, 308, 1728, 56, 8, HEADER, PANEL_EDGE);
    char shown[CONFIG_LINE_SIZE];
    ShortenToWidth(smallFont, buffer, shown, sizeof(shown), 1650);
    scene->DrawText(shown, smallFont, 116, 345, HEADER, WHITE);
    scene->DrawRectangle(118 + TextWidth(smallFont, shown), 320, 3, 32, ACCENT);

    const int cols = 12, cellW = 132, cellH = 58;
    const int startX = 70 + (1780 - cols * cellW) / 2 + 4, startY = 392;
    int count = (int)strlen(INI_KEYBOARD_CHARS);
    for (int i = 0; i < count; ++i)
    {
        int x = startX + (i % cols) * cellW;
        int y = startY + (i / cols) * cellH;
        bool selected = i == cursor;
        Color fill = selected ? ACCENT : KEY_BG;
        DrawCardEdge(scene, x, y, cellW - 8, cellH - 7, 8, fill, selected ? ACCENT : PANEL_EDGE);
        char keyLabel[8] = {};
        if (INI_KEYBOARD_CHARS[i] == ' ') CopyText(keyLabel, sizeof(keyLabel), "SPACE");
        else keyLabel[0] = INI_KEYBOARD_CHARS[i];
        scene->DrawText(keyLabel, smallFont, x + (cellW - 8 - TextWidth(smallFont, keyLabel)) / 2,
                        y + (cellH - 7) / 2 + 9, fill, selected ? BG : WHITE);
    }
    DrawButtonLegend(scene, smallFont, legend, legendCount);
}

void DrawIniKeyboard(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "INSERT"}, {"SQUARE", "BACKSPACE"}, {"TRIANGLE", "SAVE"}, {"O", "CANCEL"}};
    DrawKeyboardScreen(scene, largeFont, smallFont, "EDIT SELECTED PLUGINS.INI LINE", "LINE TEXT",
                       gIniEditBuffer, gIniKeyboardCursor, NULL, legend, 5);
}

void DrawIniBackups(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "PLUGINS.INI BACKUP HISTORY");
    DrawHeading(scene, smallFont, "BACKUPS", 150);
    char countText[48];
    snprintf(countText, sizeof(countText), "%d saved  -  newest first", gIniBackupCount);
    DrawPill(scene, smallFont, countText, 76 + TextWidth(smallFont, "BACKUPS") + 20, 126, 34, ACCENT, ACCENT_DIM);
    DrawTableShell(scene, smallFont, "CREATED", "FILE", 540);
    const int visible = TBL_VISIBLE;
    if (gIniBackupCursor < gIniBackupScroll) gIniBackupScroll = gIniBackupCursor;
    if (gIniBackupCursor >= gIniBackupScroll + visible) gIniBackupScroll = gIniBackupCursor - visible + 1;
    if (gIniBackupScroll < 0) gIniBackupScroll = 0;
    if (gIniBackupCount == 0)
        scene->DrawText((char *)"No backups found in /data/GoldHEN/plugins.ini_backups", smallFont, 122, 300, PANEL_BG, AMBER);
    for (int row = 0; row < visible; ++row)
    {
        int idx = gIniBackupScroll + row;
        if (idx >= gIniBackupCount) break;
        int y = TBL_ROW0 + row * TBL_PITCH;
        bool selected = idx == gIniBackupCursor;
        Color rowBg = selected ? CARD_ALT : ((idx & 1) ? ROW_ALT : PANEL_BG);
        if (selected) DrawCardEdge(scene, 68, y, 1772, TBL_ROW_H, 8, rowBg, ACCENT);
        else if (idx & 1) FillRound(scene, 68, y, 1772, TBL_ROW_H, 8, rowBg);
        scene->DrawText(gIniBackupLabels[idx], smallFont, 122, y + 38, rowBg, selected ? WHITE : MUTED);
        const char *base = strrchr(gIniBackupPaths[idx], '/');
        char shown[200];
        ShortenToWidth(smallFont, base ? base + 1 : gIniBackupPaths[idx], shown, sizeof(shown), 1250);
        scene->DrawText(shown, smallFont, 540, y + 38, rowBg, MUTED);
    }
    DrawScrollbar(scene, 1851, TBL_ROW0, TBL_VISIBLE * TBL_PITCH - 10, gIniBackupCount, visible, gIniBackupScroll);
    DrawStatusLine(scene, smallFont, 908, AMBER);
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"X", "RESTORE"}, {"O", "BACK"}, {"SQUARE", "DELETE"}};
    DrawButtonLegend(scene, smallFont, legend, 4);
}
