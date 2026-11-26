#include "app_state.h"
#include <orbis/Pad.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool IsRegularFile(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool HasMatchingPng(const char *pluginPath)
{
    char imagePath[600];
    CopyText(imagePath, sizeof(imagePath), pluginPath);
    char *dot = strrchr(imagePath, '.');
    if (!dot) return false;
    strcpy(dot, ".png");
    return IsRegularFile(imagePath);
}

static void AddUsbPlugin(const char *path)
{
    if (!EndsWithPluginExtension(path) || gUsbCount >= MAX_GET_USB_ITEMS) return;
    for (int i = 0; i < gUsbCount; ++i)
        if (strcasecmp(gUsbEntries[i].path, path) == 0) return;

    GetUsbEntry &entry = gUsbEntries[gUsbCount++];
    memset(&entry, 0, sizeof(entry));
    CopyText(entry.path, sizeof(entry.path), path);
    const char *base = strrchr(path, '/');
    CopyText(entry.name, sizeof(entry.name), base ? base + 1 : path);
    entry.hasImage = HasMatchingPng(path);
}

static void ScanUsbDirectory(const char *root)
{
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *item;
    while ((item = readdir(dir)) != NULL && gUsbCount < MAX_GET_USB_ITEMS)
    {
        if (!item->d_name[0] || item->d_name[0] == '.') continue;
        char full[600];
        int n = snprintf(full, sizeof(full), "%s/%s", root, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;
        if (IsRegularFile(full) && EndsWithPluginExtension(full))
            AddUsbPlugin(full);
    }
    closedir(dir);
}

void ScanGetUsbPlugins()
{
    gUsbCount = 0;
    gUsbCursor = 0;
    gUsbScroll = 0;
    memset(gUsbEntries, 0, sizeof(gUsbEntries));

    // Only scan the four explicitly approved locations; no recursive scan.
    ScanUsbDirectory("/mnt/usb0");
    ScanUsbDirectory("/mnt/usb1");
    ScanUsbDirectory("/mnt/usb0/plugins");
    ScanUsbDirectory("/mnt/usb1/plugins");

    if (gUsbCount)
        snprintf(gStatus, sizeof(gStatus), "%d PRX/SPRX files found on USB", gUsbCount);
    else
        snprintf(gStatus, sizeof(gStatus), "No PRX/SPRX files found in USB root or plugins folders");
}

static bool CopyUsbPlugin(const GetUsbEntry &entry, bool overwrite)
{
    if (!DirectoryExists(PLUGIN_ROOT))
    {
        snprintf(gStatus, sizeof(gStatus), "Plugin folder not found: %s", PLUGIN_ROOT);
        return false;
    }

    const char *base = strrchr(entry.path, '/');
    base = base ? base + 1 : entry.path;
    char target[600];
    if (snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base) >= (int)sizeof(target))
    {
        snprintf(gStatus, sizeof(gStatus), "Plugin filename is too long");
        return false;
    }
    if (FileExists(target) && !overwrite)
        return false;

    if (!CopyFileContents(entry.path, target))
    {
        snprintf(gStatus, sizeof(gStatus), "Could not copy %s", base);
        return false;
    }

    if (entry.hasImage)
    {
        char sourceImage[600], targetImage[600];
        CopyText(sourceImage, sizeof(sourceImage), entry.path);
        char *dot = strrchr(sourceImage, '.');
        if (dot) strcpy(dot, ".png");
        const char *imageBase = strrchr(sourceImage, '/');
        imageBase = imageBase ? imageBase + 1 : sourceImage;
        snprintf(targetImage, sizeof(targetImage), "%s/%s", PLUGIN_ROOT, imageBase);
        if (!FileExists(targetImage) || overwrite)
            CopyFileContents(sourceImage, targetImage);
    }
    return true;
}

static bool AnySelectedUsb()
{
    for (int i = 0; i < gUsbCount; ++i)
        if (gUsbEntries[i].selected) return true;
    return false;
}

static bool SelectedHasDuplicates()
{
    for (int i = 0; i < gUsbCount; ++i)
    {
        if (!gUsbEntries[i].selected) continue;
        const char *base = strrchr(gUsbEntries[i].path, '/');
        base = base ? base + 1 : gUsbEntries[i].path;
        char target[600];
        snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base);
        if (FileExists(target)) return true;
    }
    return false;
}

static void ImportSelectedUsb(bool overwrite)
{
    int copied = 0, failed = 0;
    for (int i = 0; i < gUsbCount; ++i)
    {
        if (!gUsbEntries[i].selected) continue;
        if (CopyUsbPlugin(gUsbEntries[i], overwrite)) ++copied;
        else ++failed;
        gUsbEntries[i].selected = false;
    }
    gGetConfirmImport = false;
    ScanGetUsbPlugins();
    snprintf(gStatus, sizeof(gStatus), "Imported %d plugin(s)%s", copied,
             failed ? " - some skipped or failed" : "");
}

static void DeleteSelectedUsb()
{
    int removed = 0, failed = 0;
    for (int i = 0; i < gUsbCount; ++i)
    {
        if (!gUsbEntries[i].selected) continue;
        if (unlink(gUsbEntries[i].path) == 0)
        {
            if (gUsbEntries[i].hasImage)
            {
                char image[600];
                CopyText(image, sizeof(image), gUsbEntries[i].path);
                char *dot = strrchr(image, '.');
                if (dot) strcpy(dot, ".png");
                unlink(image);
            }
            ++removed;
        }
        else ++failed;
    }
    gGetConfirmDelete = false;
    ScanGetUsbPlugins();
    snprintf(gStatus, sizeof(gStatus), "Deleted %d file(s)%s", removed,
             failed ? " - some failed" : "");
}

void HandleGetUsbAction(uint32_t pressed)
{
    if (gGetConfirmImport)
    {
        if (pressed & ORBIS_PAD_BUTTON_CROSS) ImportSelectedUsb(true);
        else if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { gGetConfirmImport = false; snprintf(gStatus, sizeof(gStatus), "Import cancelled"); }
        return;
    }
    if (gGetConfirmDelete)
    {
        if (pressed & ORBIS_PAD_BUTTON_CROSS) DeleteSelectedUsb();
        else if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { gGetConfirmDelete = false; snprintf(gStatus, sizeof(gStatus), "Delete cancelled"); }
        return;
    }

    if (gUsbCount > 0)
    {
        if ((pressed & ORBIS_PAD_BUTTON_UP) && gUsbCursor > 0) --gUsbCursor;
        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gUsbCursor + 1 < gUsbCount) ++gUsbCursor;
        if (pressed & ORBIS_PAD_BUTTON_L1)
        {
            gUsbEntries[gUsbCursor].selected = !gUsbEntries[gUsbCursor].selected;
            snprintf(gStatus, sizeof(gStatus), "%s: %s",
                     gUsbEntries[gUsbCursor].selected ? "Selected" : "Unselected",
                     gUsbEntries[gUsbCursor].name);
        }
        if (pressed & ORBIS_PAD_BUTTON_CROSS)
        {
            if (!AnySelectedUsb())
                gUsbEntries[gUsbCursor].selected = true;
            if (SelectedHasDuplicates())
            {
                gGetConfirmImport = true;
                snprintf(gStatus, sizeof(gStatus), "Duplicate files found. X: replace, O: cancel");
            }
            else ImportSelectedUsb(false);
        }
        if (pressed & ORBIS_PAD_BUTTON_SQUARE)
        {
            if (!AnySelectedUsb())
                gUsbEntries[gUsbCursor].selected = true;
            gGetConfirmDelete = true;
            snprintf(gStatus, sizeof(gStatus), "Delete selected USB plugin(s)? X: confirm, O: cancel");
        }
    }
    if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
    {
        gGetView = 1;
        gGetConfirmImport = gGetConfirmDelete = false;
    }
}

void DrawGetMenu(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "GET PLUGINS");
    DrawHeading(scene, smallFont, "GET PLUGINS", 150);
    DrawPill(scene, smallFont, "CHOOSE A SOURCE", 76 + TextWidth(smallFont, "GET PLUGINS") + 20, 126, 34, ACCENT, ACCENT_DIM);

    const char *items[] = {"INTERNET", "USB", "FILE BROWSER"};
    const char *details[] = {"Download plugins from the online catalog",
                             "Import plugins from connected USB storage",
                             "Browse plugin files on the console"};
    const int visible = 7;
    const int rowH = 76, rowPitch = 92, firstY = 196;
    int total = 3 + gGetShortcutCount;
    int first = 0;
    if (gGetMenuCursor >= visible) first = gGetMenuCursor - visible + 1;

    for (int row = 0; row < visible; ++row)
    {
        int index = first + row;
        if (index >= total) break;
        int y = firstY + row * rowPitch;
        char shortcutLabel[180];
        const char *label = NULL;
        const char *detail = NULL;
        if (index < 3)
        {
            label = items[index];
            detail = details[index];
        }
        else
        {
            const char *path = gGetShortcuts[index - 3];
            const char *base = strrchr(path, '/');
            snprintf(shortcutLabel, sizeof(shortcutLabel), "%s",
                     (base && base[1]) ? base + 1 : path);
            label = shortcutLabel;
            detail = path;
        }

        bool active = index == gGetMenuCursor;
        Color fill = active ? CARD_ALT : CARD;
        DrawShadow(scene, 180, y, 1560, rowH, 12);
        if (active) DrawSelectionRing(scene, 180, y, 1560, rowH, 12);
        DrawCardEdge(scene, 180, y, 1560, rowH, 12, fill, active ? ACCENT : PANEL_EDGE);
        if (active) scene->DrawRectangle(181, y + 16, 5, rowH - 32, ACCENT);

        char shownLabel[160];
        ShortenToWidthPx(label, shownLabel, sizeof(shownLabel), 560, 42);
        scene->DrawText(shownLabel, largeFont, 225, y + rowH / 2 + 14, fill, active ? WHITE : MUTED);
        char shownDetail[300];
        ShortenToWidth(smallFont, detail, shownDetail, sizeof(shownDetail), index >= 3 ? 560 : 860);
        scene->DrawText(shownDetail, smallFont, 850, y + rowH / 2 + 9, fill, active ? WHITE : MUTED);
        if (index >= 3)
        {
            const int w = TextWidth(smallFont, "SAVED FOLDER") + 38;
            DrawPill(scene, smallFont, "SAVED FOLDER", 1715 - w, y + (rowH - 38) / 2, 38, GREEN, GREEN_DIM);
        }
    }

    if (gGetConfirmShortcutDelete)
    {
        // Raised confirmation card with a clear title, explanation and actions.
        DrawPanel(scene, 360, 400, 1200, 270, HEADER);
        scene->DrawRectangle(362, 424, 6, 222, RED);
        scene->DrawText((char *)"DELETE SAVED SHORTCUT?", largeFont, 420, 465, HEADER, AMBER);
        scene->DrawText((char *)"Only the saved path will be removed.", smallFont, 420, 520, HEADER, WHITE);
        scene->DrawText((char *)"Files inside that folder will NOT be deleted.", smallFont, 420, 560, HEADER, MUTED);
        scene->DrawRectangle(420, 590, 1080, 1, DIVIDER);
        DrawPill(scene, smallFont, "X  CONFIRM DELETE", 420, 610, 42, RED, RED_DIM);
        DrawPill(scene, smallFont, "O  CANCEL", 760, 610, 42, WHITE, NEUTRAL_DIM);
    }

    const LegendButton legend[] = {{"ARROWS", "SELECT"}, {"X", "OPEN"}, {"SQUARE", "DELETE SHORTCUT"}, {"O", "BACK"}};
    DrawButtonLegend(scene, smallFont, legend, 4);
}

void DrawGetUsb(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "GET PLUGINS  /  USB");
    DrawHeading(scene, smallFont, "USB PLUGINS", 150);
    char countText[80];
    snprintf(countText, sizeof(countText), "%d plugin file%s found", gUsbCount, gUsbCount == 1 ? "" : "s");
    DrawPill(scene, smallFont, countText, 76 + TextWidth(smallFont, "USB PLUGINS") + 20, 126, 34,
             gUsbCount > 0 ? GREEN : MUTED, gUsbCount > 0 ? GREEN_DIM : NEUTRAL_DIM);
    DrawPanel(scene, 55, 178, 1810, 700, PANEL_BG);
    scene->DrawRectangle(66, 180, 1788, 40, ROW_ALT);
    scene->DrawText((char *)"FILE NAME", smallFont, 140, 209, ROW_ALT, MUTED);
    scene->DrawText((char *)"SOURCE", smallFont, 1330, 209, ROW_ALT, MUTED);
    scene->DrawRectangle(66, 220, 1788, 1, DIVIDER);
    const int visible = 9;
    if (gUsbCursor < gUsbScroll) gUsbScroll = gUsbCursor;
    if (gUsbCursor >= gUsbScroll + visible) gUsbScroll = gUsbCursor - visible + 1;
    if (gUsbScroll < 0) gUsbScroll = 0;
    if (gUsbCount == 0)
        scene->DrawText((char *)"No plugin files found on USB storage.", smallFont, 122, 300, PANEL_BG, MUTED);
    for (int row = 0; row < visible; ++row)
    {
        int idx = gUsbScroll + row;
        if (idx >= gUsbCount) break;
        int y = 232 + row * 68;
        bool active = idx == gUsbCursor;
        Color fill = active ? CARD_ALT : ((idx & 1) ? ROW_ALT : PANEL_BG);
        if (active) DrawCardEdge(scene, 68, y, 1772, 58, 8, fill, ACCENT);
        else if (idx & 1) FillRound(scene, 68, y, 1772, 58, 8, fill);
        // Checkbox: filled green when queued for import.
        const bool picked = gUsbEntries[idx].selected;
        FillRound(scene, 92, y + 14, 30, 30, 7, picked ? GREEN : MUTED);
        FillRound(scene, 94, y + 16, 26, 26, 6, picked ? GREEN : fill);
        if (picked) DrawCheckMark(scene, 92, y + 14, fill);
        char shownName[200];
        ShortenToWidth(smallFont, gUsbEntries[idx].name, shownName, sizeof(shownName), 1100);
        scene->DrawText(shownName, smallFont, 140, y + 38, fill, active ? WHITE : MUTED);
        const char *source = strstr(gUsbEntries[idx].path, "/plugins/") ? "plugins/" : "USB ROOT";
        DrawPill(scene, smallFont, source, 1312, y + 10, 38, MUTED, NEUTRAL_DIM);
        if (gUsbEntries[idx].hasImage)
            DrawPill(scene, smallFont, "PNG", 1640, y + 10, 38, GREEN, GREEN_DIM);
    }
    DrawScrollbar(scene, 1851, 232, visible * 68 - 10, gUsbCount, visible, gUsbScroll);
    if (gGetConfirmImport || gGetConfirmDelete)
    {
        DrawPanel(scene, 360, 420, 1200, 210, HEADER);
        scene->DrawRectangle(362, 444, 6, 162, gGetConfirmImport ? RED : AMBER);
        scene->DrawText((char *)(gGetConfirmImport ? "DUPLICATE PLUGINS FOUND" : "DELETE SELECTED USB FILES?"),
                        largeFont, 410, 495, HEADER, gGetConfirmImport ? RED : AMBER);
        scene->DrawText((char *)(gGetConfirmImport ? "Press X to replace existing files, O to cancel"
                                                   : "Press X to delete selected files, O to cancel"),
                        smallFont, 410, 565, HEADER, WHITE);
    }
    DrawStatusLine(scene, smallFont, 908, AMBER);
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"L1", "SELECT"}, {"X", "ADD"}, {"SQUARE", "DELETE"}, {"O", "BACK"}};
    DrawButtonLegend(scene, smallFont, legend, 5);
}


// -----------------------------------------------------------------------------
// Local File Browser and saved GET folder shortcuts
// -----------------------------------------------------------------------------
static const char *GET_SHORTCUTS_FILE = "/data/GoldHEN/plugins_manager_sources.txt";

static bool IsDirectoryPath(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool IsPluginImagePath(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && strcasecmp(dot, ".png") == 0;
}

static void NormalizeDirectoryPath(char *path)
{
    size_t n = strlen(path);
    while (n > 1 && path[n - 1] == '/')
        path[--n] = '\0';
}

void LoadGetShortcuts()
{
    gGetShortcutCount = 0;
    memset(gGetShortcuts, 0, sizeof(gGetShortcuts));
    FILE *fp = fopen(GET_SHORTCUTS_FILE, "r");
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp) && gGetShortcutCount < 32)
    {
        Trim(line);
        if (!line[0] || !IsDirectoryPath(line)) continue;

        bool duplicate = false;
        for (int i = 0; i < gGetShortcutCount; ++i)
            if (strcmp(gGetShortcuts[i], line) == 0) duplicate = true;
        if (!duplicate)
            CopyText(gGetShortcuts[gGetShortcutCount++],
                     sizeof(gGetShortcuts[0]), line);
    }
    fclose(fp);
}

bool DeleteGetShortcut(int index)
{
    LoadGetShortcuts();
    if (index < 0 || index >= gGetShortcutCount) return false;
    char tempPath[256];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", GET_SHORTCUTS_FILE);
    FILE *in = fopen(GET_SHORTCUTS_FILE, "r");
    if (!in) return false;
    FILE *out = fopen(tempPath, "w");
    if (!out) { fclose(in); return false; }
    char line[512];
    int current = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), in))
    {
        char clean[512];
        CopyText(clean, sizeof(clean), line);
        Trim(clean);
        if (!clean[0]) continue;
        if (IsDirectoryPath(clean))
        {
            if (current++ == index) continue;
        }
        if (fprintf(out, "%s\n", clean) < 0) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok || rename(tempPath, GET_SHORTCUTS_FILE) != 0)
    {
        remove(tempPath);
        snprintf(gStatus, sizeof(gStatus), "Could not delete GET shortcut");
        return false;
    }
    LoadGetShortcuts();
    snprintf(gStatus, sizeof(gStatus), "GET shortcut deleted");
    return true;
}

static bool SaveGetShortcut(const char *path)
{
    LoadGetShortcuts();
    for (int i = 0; i < gGetShortcutCount; ++i)
    {
        if (strcmp(gGetShortcuts[i], path) == 0)
        {
            snprintf(gStatus, sizeof(gStatus), "Folder is already in GET");
            return true;
        }
    }
    if (gGetShortcutCount >= 32)
    {
        snprintf(gStatus, sizeof(gStatus), "GET shortcut limit reached");
        return false;
    }

    FILE *fp = fopen(GET_SHORTCUTS_FILE, "a");
    if (!fp)
    {
        snprintf(gStatus, sizeof(gStatus), "Cannot save GET shortcut");
        return false;
    }
    fprintf(fp, "%s\n", path);
    fclose(fp);
    LoadGetShortcuts();
    snprintf(gStatus, sizeof(gStatus), "Folder shortcut added to GET");
    return true;
}

static bool AddBrowserEntry(const char *path, const char *name, bool isDir, unsigned long size)
{
    if (gBrowserCount >= MAX_GET_USB_ITEMS) return false;
    GetBrowserEntry &entry = gBrowserEntries[gBrowserCount++];
    memset(&entry, 0, sizeof(entry));
    CopyText(entry.path, sizeof(entry.path), path);
    CopyText(entry.name, sizeof(entry.name), name);
    entry.isDirectory = isDir;
    entry.size = isDir ? 0 : size;
    entry.hasImage = !isDir && EndsWithPluginExtension(path) && HasMatchingPng(path);
    return true;
}

static int CompareBrowserEntries(const void *a, const void *b)
{
    const GetBrowserEntry *ea = (const GetBrowserEntry *)a;
    const GetBrowserEntry *eb = (const GetBrowserEntry *)b;
    if (ea->isDirectory != eb->isDirectory) return ea->isDirectory ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

void ScanGetBrowserDirectory()
{
    gBrowserCount = 0;
    gBrowserCursor = 0;
    gBrowserScroll = 0;
    memset(gBrowserEntries, 0, sizeof(gBrowserEntries));

    DIR *dir = opendir(gBrowserPath);
    if (!dir)
    {
        snprintf(gStatus, sizeof(gStatus), "Cannot open folder: %.150s", gBrowserPath);
        return;
    }

    struct dirent *item;
    while ((item = readdir(dir)) != NULL && gBrowserCount < MAX_GET_USB_ITEMS)
    {
        if (!item->d_name[0] || strcmp(item->d_name, ".") == 0 ||
            strcmp(item->d_name, "..") == 0)
            continue;

        char full[600];
        int n = snprintf(full, sizeof(full), "%s%s%s",
                         gBrowserPath,
                         strcmp(gBrowserPath, "/") == 0 ? "" : "/",
                         item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;

        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;
        AddBrowserEntry(full, item->d_name, S_ISDIR(st.st_mode), (unsigned long)st.st_size);
    }
    closedir(dir);

    // Keep folders first, followed by files, each alphabetically.
    if (gBrowserCount > 1)
        qsort(gBrowserEntries, (size_t)gBrowserCount, sizeof(GetBrowserEntry), CompareBrowserEntries);
}

static bool CopyBrowserPlugin(const GetBrowserEntry &entry, bool overwrite)
{
    if (!EndsWithPluginExtension(entry.path) || !IsRegularFile(entry.path))
        return false;
    if (!DirectoryExists(PLUGIN_ROOT))
    {
        snprintf(gStatus, sizeof(gStatus), "Plugin folder not found");
        return false;
    }

    const char *base = strrchr(entry.path, '/');
    base = base ? base + 1 : entry.path;
    char target[600];
    if (snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base) >= (int)sizeof(target))
        return false;
    if (FileExists(target) && !overwrite)
        return false;
    if (!CopyFileContents(entry.path, target))
    {
        snprintf(gStatus, sizeof(gStatus), "Could not copy %.100s", base);
        return false;
    }

    if (HasMatchingPng(entry.path))
    {
        char sourceImage[600], targetImage[600];
        CopyText(sourceImage, sizeof(sourceImage), entry.path);
        char *dot = strrchr(sourceImage, '.');
        if (dot) strcpy(dot, ".png");
        const char *imageBase = strrchr(sourceImage, '/');
        imageBase = imageBase ? imageBase + 1 : sourceImage;
        snprintf(targetImage, sizeof(targetImage), "%s/%s", PLUGIN_ROOT, imageBase);
        if (!FileExists(targetImage) || overwrite)
            CopyFileContents(sourceImage, targetImage);
    }
    return true;
}

static bool BrowserSelectionHasDuplicates()
{
    for (int i = 0; i < gBrowserCount; ++i)
    {
        if (!gBrowserEntries[i].selected || gBrowserEntries[i].isDirectory ||
            !EndsWithPluginExtension(gBrowserEntries[i].path))
            continue;
        const char *base = strrchr(gBrowserEntries[i].path, '/');
        base = base ? base + 1 : gBrowserEntries[i].path;
        char target[600];
        snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base);
        if (FileExists(target)) return true;
    }
    return false;
}

static void ImportBrowserSelection(bool overwrite)
{
    int copied = 0, failed = 0;
    for (int i = 0; i < gBrowserCount; ++i)
    {
        if (!gBrowserEntries[i].selected || gBrowserEntries[i].isDirectory ||
            !EndsWithPluginExtension(gBrowserEntries[i].path))
            continue;
        if (CopyBrowserPlugin(gBrowserEntries[i], overwrite)) ++copied;
        else ++failed;
        gBrowserEntries[i].selected = false;
    }
    gGetConfirmImport = false;
    ScanGetBrowserDirectory();
    snprintf(gStatus, sizeof(gStatus), "Imported %d plugin(s)%s", copied,
             failed ? " - some skipped or failed" : "");
}

static int DeleteBrowserPath(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    if (S_ISDIR(st.st_mode))
    {
        DIR *dir = opendir(path);
        if (!dir) return -1;
        struct dirent *item;
        while ((item = readdir(dir)) != NULL)
        {
            if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
            char child[600];
            int n = snprintf(child, sizeof(child), "%s/%s", path, item->d_name);
            if (n <= 0 || (size_t)n >= sizeof(child) || DeleteBrowserPath(child) != 0)
            {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
        return rmdir(path);
    }
    return unlink(path);
}

static void DeleteBrowserSelection()
{
    int removed = 0, failed = 0;
    for (int i = 0; i < gBrowserCount; ++i)
    {
        if (!gBrowserEntries[i].selected) continue;
        if (DeleteBrowserPath(gBrowserEntries[i].path) == 0)
        {
            // If a plugin is removed, remove its matching PNG as well.
            if (!gBrowserEntries[i].isDirectory &&
                EndsWithPluginExtension(gBrowserEntries[i].path) &&
                gBrowserEntries[i].hasImage)
            {
                char image[600];
                CopyText(image, sizeof(image), gBrowserEntries[i].path);
                char *dot = strrchr(image, '.');
                if (dot) strcpy(dot, ".png");
                unlink(image);
            }
            ++removed;
        }
        else ++failed;
    }
    gBrowserConfirmDelete = false;
    ScanGetBrowserDirectory();
    snprintf(gStatus, sizeof(gStatus), "Deleted %d item(s)%s", removed,
             failed ? " - some failed" : "");
}

static bool FolderHasDuplicatePlugins(const char *folder, int depth)
{
    if (depth > 12) return false;
    DIR *dir = opendir(folder);
    if (!dir) return false;
    bool found = false;
    struct dirent *item;
    while (!found && (item = readdir(dir)) != NULL)
    {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        char full[600];
        int n = snprintf(full, sizeof(full), "%s/%s", folder, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode))
            found = FolderHasDuplicatePlugins(full, depth + 1);
        else if (S_ISREG(st.st_mode) && EndsWithPluginExtension(full))
        {
            const char *base = strrchr(full, '/');
            base = base ? base + 1 : full;
            char target[600];
            snprintf(target, sizeof(target), "%s/%s", PLUGIN_ROOT, base);
            if (FileExists(target)) found = true;
        }
    }
    closedir(dir);
    return found;
}

static void ImportPluginsRecursive(const char *folder, int depth, bool overwrite,
                                   int *copied, int *failed)
{
    if (depth > 12) return;
    DIR *dir = opendir(folder);
    if (!dir) { ++*failed; return; }
    struct dirent *item;
    while ((item = readdir(dir)) != NULL)
    {
        if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, "..")) continue;
        char full[600];
        int n = snprintf(full, sizeof(full), "%s/%s", folder, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) { ++*failed; continue; }

        struct stat st;
        if (stat(full, &st) != 0) { ++*failed; continue; }
        if (S_ISDIR(st.st_mode))
            ImportPluginsRecursive(full, depth + 1, overwrite, copied, failed);
        else if (S_ISREG(st.st_mode) && EndsWithPluginExtension(full))
        {
            GetBrowserEntry entry = {};
            CopyText(entry.path, sizeof(entry.path), full);
            entry.hasImage = HasMatchingPng(full);
            if (CopyBrowserPlugin(entry, overwrite)) ++*copied;
            else ++*failed;
        }
    }
    closedir(dir);
}

void HandleGetBrowserAction(uint32_t pressed)
{
    if (gBrowserConfirmDelete)
    {
        if (pressed & ORBIS_PAD_BUTTON_CROSS) DeleteBrowserSelection();
        else if (pressed & ORBIS_PAD_BUTTON_CIRCLE) { gBrowserConfirmDelete = false; snprintf(gStatus, sizeof(gStatus), "Delete cancelled"); }
        return;
    }
    if (gGetConfirmImport)
    {
        if (pressed & ORBIS_PAD_BUTTON_CROSS)
        {
            if (gBrowserFolderImportPending)
            {
                int copied = 0, failed = 0;
                ImportPluginsRecursive(gBrowserPath, 0, true, &copied, &failed);
                gBrowserFolderImportPending = false;
                gGetConfirmImport = false;
                snprintf(gStatus, sizeof(gStatus), "Imported %d plugin(s), %d skipped/failed", copied, failed);
                ScanGetBrowserDirectory();
            }
            else ImportBrowserSelection(true);
        }
        else if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
        {
            gGetConfirmImport = false;
            gBrowserFolderImportPending = false;
            snprintf(gStatus, sizeof(gStatus), "Import cancelled");
        }
        return;
    }
    if (gBrowserFolderOptions)
    {
        if (pressed & ORBIS_PAD_BUTTON_UP) gBrowserFolderOptionCursor = 0;
        if (pressed & ORBIS_PAD_BUTTON_DOWN) gBrowserFolderOptionCursor = 1;
        if (pressed & ORBIS_PAD_BUTTON_CIRCLE) gBrowserFolderOptions = false;
        if (pressed & ORBIS_PAD_BUTTON_CROSS)
        {
            if (gBrowserFolderOptionCursor == 0)
            {
                const char *folder = gBrowserPath;
                gBrowserFolderOptions = false;
                if (FolderHasDuplicatePlugins(folder, 0))
                {
                    gBrowserFolderImportPending = true;
                    gGetConfirmImport = true;
                    snprintf(gStatus, sizeof(gStatus), "Duplicate files found. X: replace, O: cancel");
                }
                else
                {
                    int copied = 0, failed = 0;
                    ImportPluginsRecursive(folder, 0, false, &copied, &failed);
                    snprintf(gStatus, sizeof(gStatus), "Imported %d plugin(s), %d skipped/failed", copied, failed);
                    ScanGetBrowserDirectory();
                }
            }
            else
            {
                SaveGetShortcut(gBrowserPath);
                gBrowserFolderOptions = false;
                // After creating a shortcut, Circle returns directly to GET.
                gBrowserShortcutMode = true;
            }
        }
        return;
    }

    if (gBrowserCount > 0)
    {
        if ((pressed & ORBIS_PAD_BUTTON_UP) && gBrowserCursor > 0) --gBrowserCursor;
        if ((pressed & ORBIS_PAD_BUTTON_DOWN) && gBrowserCursor + 1 < gBrowserCount) ++gBrowserCursor;

        if (pressed & ORBIS_PAD_BUTTON_L1)
        {
            GetBrowserEntry &entry = gBrowserEntries[gBrowserCursor];
            if (!entry.isDirectory && EndsWithPluginExtension(entry.path))
            {
                entry.selected = !entry.selected;
                snprintf(gStatus, sizeof(gStatus), "%s: %.120s",
                         entry.selected ? "Selected" : "Unselected", entry.name);
            }
            else snprintf(gStatus, sizeof(gStatus), "Only PRX/SPRX files can be selected");
        }

        if (pressed & ORBIS_PAD_BUTTON_CROSS)
        {
            GetBrowserEntry &entry = gBrowserEntries[gBrowserCursor];
            if (entry.isDirectory)
            {
                CopyText(gBrowserPath, sizeof(gBrowserPath), entry.path);
                NormalizeDirectoryPath(gBrowserPath);
                ScanGetBrowserDirectory();
            }
            else if (EndsWithPluginExtension(entry.path))
            {
                bool anySelected = false;
                for (int i = 0; i < gBrowserCount; ++i)
                    if (gBrowserEntries[i].selected) anySelected = true;
                if (!anySelected) entry.selected = true;
                if (BrowserSelectionHasDuplicates())
                {
                    gGetConfirmImport = true;
                    snprintf(gStatus, sizeof(gStatus), "Duplicate files found. X: replace, O: cancel");
                }
                else ImportBrowserSelection(false);
            }
        }

        if (pressed & ORBIS_PAD_BUTTON_SQUARE)
        {
            bool anySelected = false;
            for (int i = 0; i < gBrowserCount; ++i)
                if (gBrowserEntries[i].selected) anySelected = true;
            if (!anySelected) gBrowserEntries[gBrowserCursor].selected = true;
            gBrowserConfirmDelete = true;
            snprintf(gStatus, sizeof(gStatus), "Delete selected item(s)? X: confirm, O: cancel");
        }

    }

    // Triangle always opens actions for the directory currently being viewed,
    // including an empty directory, and never depends on the highlighted row.
    if (pressed & ORBIS_PAD_BUTTON_TRIANGLE)
    {
        gBrowserFolderOptions = true;
        gBrowserFolderOptionCursor = 0;
    }

    if (pressed & ORBIS_PAD_BUTTON_CIRCLE)
    {
        if (gBrowserShortcutMode || strcmp(gBrowserPath, "/") == 0)
        {
            gBrowserShortcutMode = false;
            LoadGetShortcuts();
            gGetView = 1;
            gStatus[0] = '\0';
        }
        else
        {
            char cameFrom[160] = {};
            const char *lastSlash = strrchr(gBrowserPath, '/');
            if (lastSlash) CopyText(cameFrom, sizeof(cameFrom), lastSlash + 1);
            char *slash = strrchr(gBrowserPath, '/');
            if (slash && slash != gBrowserPath) *slash = '\0';
            else CopyText(gBrowserPath, sizeof(gBrowserPath), "/");
            ScanGetBrowserDirectory();
            for (int i = 0; cameFrom[0] && i < gBrowserCount; ++i)
            {
                if (strcmp(gBrowserEntries[i].name, cameFrom) != 0) continue;
                gBrowserCursor = i;
                gBrowserScroll = i > 3 ? i - 3 : 0;
                break;
            }
        }
    }
}

// "812 B", "14.2 KB", "3.4 MB" (integer maths only).
static void FormatFileSize(unsigned long bytes, char *out, size_t outSize)
{
    if (bytes < 1024UL) snprintf(out, outSize, "%lu B", bytes);
    else if (bytes < 1024UL * 1024UL)
    {
        const unsigned long tenths = bytes * 10UL / 1024UL;
        snprintf(out, outSize, "%lu.%lu KB", tenths / 10UL, tenths % 10UL);
    }
    else if (bytes < 1024UL * 1024UL * 1024UL)
    {
        const unsigned long tenths = bytes * 10UL / (1024UL * 1024UL);
        snprintf(out, outSize, "%lu.%lu MB", tenths / 10UL, tenths % 10UL);
    }
    else
    {
        const unsigned long hundredths = bytes / (1024UL * 1024UL * 1024UL / 100UL);
        snprintf(out, outSize, "%lu.%02lu GB", hundredths / 100UL, hundredths % 100UL);
    }
}

// Raised dialog with a coloured edge bar; callers add their own text and buttons.
static void DrawBrowserDialog(Scene2D *scene, int x, int y, int w, int h, Color bar)
{
    DrawPanel(scene, x, y, w, h, HEADER);
    FillRound(scene, x + 2, y + 22, 6, h - 44, 3, bar);
}

void DrawGetBrowser(Scene2D *scene, FT_Face largeFont, FT_Face smallFont)
{
    DrawAppChrome(scene, largeFont, smallFont, "GET PLUGINS  /  FILE BROWSER");

    int folderCount = 0, pluginCount = 0, selectedCount = 0;
    for (int i = 0; i < gBrowserCount; ++i)
    {
        if (gBrowserEntries[i].isDirectory) ++folderCount;
        else if (EndsWithPluginExtension(gBrowserEntries[i].path)) ++pluginCount;
        if (gBrowserEntries[i].selected) ++selectedCount;
    }

    // Heading with live counts.
    DrawHeading(scene, smallFont, "FILE BROWSER", 150);
    char counts[80];
    snprintf(counts, sizeof(counts), "%d folder%s  -  %d plugin%s", folderCount, folderCount == 1 ? "" : "s",
             pluginCount, pluginCount == 1 ? "" : "s");
    int pillX = 76 + TextWidth(smallFont, "FILE BROWSER") + 20;
    pillX += DrawPill(scene, smallFont, counts, pillX, 126, 34, ACCENT, ACCENT_DIM) + 12;
    if (selectedCount > 0)
    {
        char selectedText[48];
        snprintf(selectedText, sizeof(selectedText), "%d selected", selectedCount);
        DrawPill(scene, smallFont, selectedText, pillX, 126, 34, GREEN, GREEN_DIM);
    }

    // Path bar: always shows where you are (the end of long paths is kept).
    DrawCardEdge(scene, 55, 170, 1810, 46, 10, HEADER, PANEL_EDGE);
    DrawFolderGlyph(scene, 78, 181, 26, AMBER);
    char shownPath[600];
    ShortenPathToWidth(gBrowserPath, shownPath, sizeof(shownPath), 1680);
    scene->DrawText(shownPath, smallFont, 122, 202, HEADER, WHITE);

    // File list.
    const int panelY = 228, panelH = 650;
    const int rowTop0 = 282, rowPitch = 64, rowH = 56, visible = 9;
    DrawPanel(scene, 55, panelY, 1810, panelH, PANEL_BG);
    scene->DrawRectangle(66, panelY + 2, 1788, 40, ROW_ALT);
    scene->DrawText((char *)"NAME", smallFont, 190, panelY + 31, ROW_ALT, MUTED);
    scene->DrawText((char *)"SIZE", smallFont, 1480 - TextWidth(smallFont, "SIZE"), panelY + 31, ROW_ALT, MUTED);
    scene->DrawText((char *)"TYPE", smallFont, 1578, panelY + 31, ROW_ALT, MUTED);
    scene->DrawRectangle(66, panelY + 42, 1788, 1, DIVIDER);

    if (gBrowserCursor < gBrowserScroll) gBrowserScroll = gBrowserCursor;
    if (gBrowserCursor >= gBrowserScroll + visible) gBrowserScroll = gBrowserCursor - visible + 1;
    if (gBrowserScroll < 0) gBrowserScroll = 0;

    if (gBrowserCount == 0)
    {
        DrawFolderGlyph(scene, 960 - 30, 430, 60, NEUTRAL_DIM);
        const char *title = "This folder is empty";
        scene->DrawText((char *)title, largeFont, 960 - TextWidthPx(title, 42) / 2, 560, PANEL_BG, WHITE);
        const char *hint = "TRIANGLE: folder options        O: go back";
        scene->DrawText((char *)hint, smallFont, 960 - TextWidth(smallFont, hint) / 2, 620, PANEL_BG, MUTED);
    }

    const Color imageColor = {181, 145, 218}, imageDim = {50, 40, 66};
    const Color pluginTint = {25, 40, 54};
    for (int row = 0; row < visible; ++row)
    {
        const int idx = gBrowserScroll + row;
        if (idx >= gBrowserCount) break;
        const int rowTop = rowTop0 + row * rowPitch;
        const GetBrowserEntry &entry = gBrowserEntries[idx];
        const bool active = idx == gBrowserCursor;
        const bool plugin = !entry.isDirectory && EndsWithPluginExtension(entry.path);
        const bool imageFile = !entry.isDirectory && IsPluginImagePath(entry.path);

        Color fill = active ? CARD_ALT : (plugin ? pluginTint : ((idx & 1) ? ROW_ALT : PANEL_BG));
        if (active) DrawCardEdge(scene, 68, rowTop, 1772, rowH, 8, fill, ACCENT);
        else if (plugin || (idx & 1)) FillRound(scene, 68, rowTop, 1772, rowH, 8, fill);
        if (plugin && !active) scene->DrawRectangle(69, rowTop + 10, 3, rowH - 20, ACCENT);

        // Checkbox (plugin files only): filled green when queued for import.
        if (plugin)
        {
            const int bx = 92, by = rowTop + (rowH - 30) / 2;
            FillRound(scene, bx, by, 30, 30, 7, entry.selected ? GREEN : MUTED);
            FillRound(scene, bx + 2, by + 2, 26, 26, 6, entry.selected ? GREEN : fill);
            if (entry.selected) DrawCheckMark(scene, bx, by, fill);
        }

        // Type icon.
        const int gx = 138, gy = rowTop + (rowH - 30) / 2;
        if (entry.isDirectory) DrawFolderGlyph(scene, gx, gy + 2, 30, AMBER);
        else if (plugin) DrawPlugGlyph(scene, gx + 15, gy + 15, 30, ACCENT, fill);
        else DrawFileGlyph(scene, gx, gy, 30, imageFile ? imageColor : MUTED, fill);

        // Name.
        char shownName[200];
        ShortenToWidth(smallFont, entry.name, shownName, sizeof(shownName), 1180);
        scene->DrawText(shownName, smallFont, 190, rowTop + rowH / 2 + 9, fill,
                        active ? WHITE : (plugin ? WHITE : MUTED));

        // Size (files only), right-aligned.
        if (!entry.isDirectory)
        {
            char sizeText[32];
            FormatFileSize(entry.size, sizeText, sizeof(sizeText));
            scene->DrawText(sizeText, smallFont, 1480 - TextWidth(smallFont, sizeText),
                            rowTop + rowH / 2 + 9, fill, MUTED);
        }

        // Type badge (+ PNG badge when a plugin has matching artwork).
        const char *type = entry.isDirectory ? "FOLDER" : (plugin ? "PLUGIN" : (imageFile ? "IMAGE" : "FILE"));
        Color typeFg = entry.isDirectory ? AMBER : (plugin ? ACCENT : (imageFile ? imageColor : MUTED));
        Color typeBg = entry.isDirectory ? AMBER_DIM : (plugin ? ACCENT_DIM : (imageFile ? imageDim : NEUTRAL_DIM));
        const int typeW = DrawPill(scene, smallFont, type, 1560, rowTop + (rowH - 36) / 2, 36, typeFg, typeBg);
        if (plugin && entry.hasImage)
            DrawPill(scene, smallFont, "PNG", 1560 + typeW + 10, rowTop + (rowH - 36) / 2, 36, GREEN, GREEN_DIM);
    }
    DrawScrollbar(scene, 1851, rowTop0, visible * rowPitch - 8, gBrowserCount, visible, gBrowserScroll);

    // Dialogs.
    if (gBrowserFolderOptions)
    {
        DrawBrowserDialog(scene, 430, 320, 1060, 390, ACCENT);
        scene->DrawText((char *)"CURRENT FOLDER", largeFont, 490, 385, HEADER, WHITE);
        char dialogPath[600];
        ShortenPathToWidth(gBrowserPath, dialogPath, sizeof(dialogPath), 940);
        scene->DrawText(dialogPath, smallFont, 492, 425, HEADER, MUTED);
        const char *labels[2] = {"IMPORT ALL PLUGINS", "ADD TO GET"};
        const char *details[2] = {"Copy all plugins from this folder and its subfolders",
                                  "Save this folder as a shortcut in the GET menu"};
        for (int i = 0; i < 2; ++i)
        {
            const int cy = 450 + i * 86;
            const bool selected = i == gBrowserFolderOptionCursor;
            Color optionFill = selected ? CARD_ALT : PANEL_BG;
            DrawCardEdge(scene, 470, cy, 980, 74, 10, optionFill, selected ? ACCENT : PANEL_EDGE);
            scene->DrawText((char *)labels[i], smallFont, 505, cy + 33, optionFill, selected ? WHITE : MUTED);
            scene->DrawText((char *)details[i], smallFont, 505, cy + 61, optionFill, MUTED);
        }
        DrawPill(scene, smallFont, "X  SELECT", 470, 640, 40, WHITE, NEUTRAL_DIM);
        DrawPill(scene, smallFont, "O  BACK", 690, 640, 40, WHITE, NEUTRAL_DIM);
    }
    else if (gBrowserConfirmDelete || gGetConfirmImport)
    {
        const Color accent = gBrowserConfirmDelete ? RED : AMBER;
        DrawBrowserDialog(scene, 360, 380, 1200, 300, accent);
        scene->DrawText((char *)(gBrowserConfirmDelete ? "DELETE SELECTED ITEMS?" : "REPLACE EXISTING PLUGINS?"),
                        largeFont, 420, 445, HEADER, accent);
        char line[160];
        if (gBrowserConfirmDelete)
        {
            snprintf(line, sizeof(line), "%d item%s will be permanently removed.", selectedCount,
                     selectedCount == 1 ? "" : "s");
        }
        else CopyText(line, sizeof(line), "Files with matching names in the plugins folder will be replaced.");
        scene->DrawText(line, smallFont, 420, 505, HEADER, WHITE);
        scene->DrawText((char *)(gBrowserConfirmDelete
                                    ? "Folder contents are included in the deletion."
                                    : "Matching PNG images will also be copied when available."),
                        smallFont, 420, 548, HEADER, MUTED);
        scene->DrawRectangle(420, 578, 1080, 1, DIVIDER);
        DrawPill(scene, smallFont, gBrowserConfirmDelete ? "X  DELETE" : "X  REPLACE", 420, 600, 42,
                 accent, gBrowserConfirmDelete ? RED_DIM : AMBER_DIM);
        DrawPill(scene, smallFont, "O  CANCEL", 700, 600, 42, WHITE, NEUTRAL_DIM);
    }

    DrawStatusLine(scene, smallFont, 908, AMBER);
    const LegendButton legend[] = {{"ARROWS", "MOVE"}, {"L1", "SELECT"}, {"X", "OPEN / ADD"}, {"TRIANGLE", "FOLDER OPTIONS"}, {"SQUARE", "DELETE"}, {"O", "BACK"}};
    DrawButtonLegend(scene, smallFont, legend, 6);
}
