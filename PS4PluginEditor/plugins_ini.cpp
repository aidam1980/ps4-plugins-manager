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
bool CopyFileContents(const char *source, const char *target)
{
    FILE *in = fopen(source, "rb");
    if (!in) return false;
    FILE *out = fopen(target, "wb");
    if (!out) { fclose(in); return false; }
    unsigned char buffer[8192];
    bool ok = true;
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
    {
        if (fwrite(buffer, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    if (fclose(in) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    return ok;
}

// Backups are manual-only: changing plugins.ini must never create one implicitly.
bool CreateBackupIfNeeded()
{
    return true;
}

bool LoadConfigLines()
{
    gConfigLineCount = 0;
    FILE *f = fopen(PLUGINS_INI, "r");
    if (!f) return true; // file can be created on first write
    char line[CONFIG_LINE_SIZE];
    while (fgets(line, sizeof(line), f))
    {
        if (gConfigLineCount >= MAX_CONFIG_LINES)
        {
            fclose(f);
            snprintf(gStatus, sizeof(gStatus), "plugins.ini has too many lines; left unchanged");
            return false;
        }
        CopyText(gConfigLines[gConfigLineCount++], CONFIG_LINE_SIZE, line);
    }
    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

bool CreateTimestampedBackup(char *createdPath, size_t createdPathSize)
{
    if (createdPath && createdPathSize) createdPath[0] = '\0';
    if (!FileExists(PLUGINS_INI)) return false;

    const char *backupDir = "/data/GoldHEN/plugins.ini_backups";
    // Create the dedicated backup directory if it does not exist yet.
    if (mkdir(backupDir, 0777) != 0 && errno != EEXIST) return false;

    time_t now = time(NULL);
    struct tm tmInfo;
    if (!localtime_r(&now, &tmInfo)) return false;

    char backupPath[320];
    char stamp[40];
    if (strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tmInfo) == 0) return false;
    snprintf(backupPath, sizeof(backupPath), "%s/plugins.ini_%s.bak", backupDir, stamp);

    // Do not overwrite a backup if two manual saves happen within one second.
    for (int suffix = 1; FileExists(backupPath) && suffix < 100; ++suffix)
        snprintf(backupPath, sizeof(backupPath), "%s/plugins.ini_%s_%d.bak", backupDir, stamp, suffix);
    if (FileExists(backupPath) || !CopyFileContents(PLUGINS_INI, backupPath)) return false;
    if (createdPath && createdPathSize)
        snprintf(createdPath, createdPathSize, "%s", backupPath);
    return true;
}

bool SaveConfigLinesSafely(bool makeBackup)
{
    if (!DirectoryExists("/data/GoldHEN"))
    {
        snprintf(gStatus, sizeof(gStatus), "Missing /data/GoldHEN; plugins.ini not changed");
        return false;
    }
    if (makeBackup && FileExists(PLUGINS_INI) && !CreateTimestampedBackup())
    {
        snprintf(gStatus, sizeof(gStatus), "Backup failed; plugins.ini was not changed");
        return false;
    }
    FILE *out = fopen(TEMP_INI, "wb");
    if (!out)
    {
        snprintf(gStatus, sizeof(gStatus), "Cannot write temporary plugins.ini");
        return false;
    }
    bool ok = true;
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
    return true;
}


bool CommitIniEdit()
{
    if (gIniCursor < 0 || gIniCursor >= gConfigLineCount || gIniEditBuffer[0] == '\0')
    {
        snprintf(gStatus, sizeof(gStatus), "Empty line not saved; use delete to remove a line");
        return false;
    }
    // Preserve the line terminator convention used by plugins.ini.
    snprintf(gConfigLines[gIniCursor], CONFIG_LINE_SIZE, "%s\n", gIniEditBuffer);
    if (!SaveConfigLinesSafely(false)) return false;
    LoadConfigLines();
    if (gIniCursor >= gConfigLineCount) gIniCursor = gConfigLineCount > 0 ? gConfigLineCount - 1 : 0;
    snprintf(gStatus, sizeof(gStatus), "Line %d updated", gIniCursor + 1);
    return true;
}

bool DeleteSelectedIniLine()
{
    if (gIniCursor < 0 || gIniCursor >= gConfigLineCount)
    {
        snprintf(gStatus, sizeof(gStatus), "Select a line to delete first");
        return false;
    }
    for (int i = gIniCursor; i + 1 < gConfigLineCount; ++i)
        CopyText(gConfigLines[i], CONFIG_LINE_SIZE, gConfigLines[i + 1]);
    if (gConfigLineCount > 0) --gConfigLineCount;
    if (!SaveConfigLinesSafely(false)) return false;
    if (gIniCursor >= gConfigLineCount) gIniCursor = gConfigLineCount > 0 ? gConfigLineCount - 1 : 0;
    snprintf(gStatus, sizeof(gStatus), "Line deleted");
    return true;
}

void RefreshIniBackups()
{
    gIniBackupCount = 0;
    DIR *dir = opendir("/data/GoldHEN/plugins.ini_backups");
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && gIniBackupCount < 32)
    {
        if (strncmp(entry->d_name, "plugins.ini_", 11) != 0) continue;
        char full[256];
        snprintf(full, sizeof(full), "/data/GoldHEN/plugins.ini_backups/%s", entry->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        int slot = gIniBackupCount++;
        CopyText(gIniBackupPaths[slot], sizeof(gIniBackupPaths[slot]), full);
        // Display the timestamp embedded in the backup filename, not filesystem mtime.
        int yy, mo, dd, hh, mm, ss;
        if (sscanf(entry->d_name, "plugins.ini_%4d-%2d-%2d_%2d-%2d-%2d.bak", &yy, &mo, &dd, &hh, &mm, &ss) == 6)
            snprintf(gIniBackupLabels[slot], sizeof(gIniBackupLabels[slot]), "%04d-%02d-%02d %02d:%02d:%02d", yy, mo, dd, hh, mm, ss);
        else CopyText(gIniBackupLabels[slot], sizeof(gIniBackupLabels[slot]), entry->d_name);
    }
    closedir(dir);
    // Sort newest first using the backup file modification time.
    for (int i = 0; i < gIniBackupCount; ++i)
        for (int j = i + 1; j < gIniBackupCount; ++j)
        {
            if (strcmp(gIniBackupPaths[j], gIniBackupPaths[i]) > 0)
            {
                char pathTmp[256], labelTmp[96];
                CopyText(pathTmp, sizeof(pathTmp), gIniBackupPaths[i]);
                CopyText(gIniBackupPaths[i], sizeof(gIniBackupPaths[i]), gIniBackupPaths[j]);
                CopyText(gIniBackupPaths[j], sizeof(gIniBackupPaths[j]), pathTmp);
                CopyText(labelTmp, sizeof(labelTmp), gIniBackupLabels[i]);
                CopyText(gIniBackupLabels[i], sizeof(gIniBackupLabels[i]), gIniBackupLabels[j]);
                CopyText(gIniBackupLabels[j], sizeof(gIniBackupLabels[j]), labelTmp);
            }
        }
    if (gIniBackupCursor >= gIniBackupCount) gIniBackupCursor = gIniBackupCount > 0 ? gIniBackupCount - 1 : 0;
}

bool RestoreSelectedIniBackup()
{
    if (gIniBackupCursor < 0 || gIniBackupCursor >= gIniBackupCount)
    {
        snprintf(gStatus, sizeof(gStatus), "No backup selected");
        return false;
    }
    if (!CopyFileContents(gIniBackupPaths[gIniBackupCursor], TEMP_INI) || rename(TEMP_INI, PLUGINS_INI) != 0)
    {
        remove(TEMP_INI);
        snprintf(gStatus, sizeof(gStatus), "Restore failed");
        return false;
    }
    LoadConfigLines();
    LoadConfigEntries();
    gIniCursor = 0;
    snprintf(gStatus, sizeof(gStatus), "Backup restored: %s", gIniBackupLabels[gIniBackupCursor]);
    return true;
}

bool DeleteSelectedIniBackup()
{
    if (gIniBackupCursor < 0 || gIniBackupCursor >= gIniBackupCount)
    {
        snprintf(gStatus, sizeof(gStatus), "No backup selected");
        return false;
    }

    char deletedLabel[96];
    CopyText(deletedLabel, sizeof(deletedLabel), gIniBackupLabels[gIniBackupCursor]);
    if (remove(gIniBackupPaths[gIniBackupCursor]) != 0)
    {
        snprintf(gStatus, sizeof(gStatus), "Could not delete backup");
        return false;
    }

    RefreshIniBackups();
    snprintf(gStatus, sizeof(gStatus), "Backup deleted: %s", deletedLabel);
    return true;
}

