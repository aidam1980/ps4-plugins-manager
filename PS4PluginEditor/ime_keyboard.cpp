#include "app_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <orbis/ImeDialog.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>


// Native PS4 IME dialog handling lives here. INI persistence remains in plugins_ini.cpp.
// The IME initialization and result flow below follow ItemzFlow's Keyboard()
// pattern while retaining this project's line-editing and save logic.
bool EditSelectedIniLine(int32_t userID)
{
    (void)userID; // ItemzFlow uses the system IME user ID 0xFE.
    if (gIniCursor < 0 || gIniCursor >= gConfigLineCount)
    {
        snprintf(gStatus, sizeof(gStatus), "Select a line to edit first");
        return false;
    }

    // Match ItemzFlow's native IME initialization pattern:
    // load IME, zero-initialize the parameter struct, and set only the
    // fields used by ItemzFlow's Keyboard() implementation.
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG) < 0)
    {
        snprintf(gStatus, sizeof(gStatus), "Could not load PS4 on-screen keyboard module");
        return false;
    }

    char original[CONFIG_LINE_SIZE] = {};
    CopyText(original, sizeof(original), gConfigLines[gIniCursor]);
    size_t originalLen = strlen(original);
    while (originalLen && (original[originalLen - 1] == '\n' || original[originalLen - 1] == '\r'))
        original[--originalLen] = '\0';

    wchar_t inputTextBuffer[CONFIG_LINE_SIZE] = {};
    if (mbstowcs(inputTextBuffer, original, CONFIG_LINE_SIZE - 1) == (size_t)-1)
    {
        snprintf(gStatus, sizeof(gStatus), "Could not prepare text for PS4 keyboard");
        return false;
    }

    static const wchar_t title[] = L"Edit plugins.ini line";

    OrbisImeDialogSetting setting = {};
    setting.userId = 0xFE; // ItemzFlow uses 0xFE for the native keyboard.
    setting.type = ORBIS_TYPE_BASIC_LATIN;
    setting.enterLabel = ORBIS_BUTTON_LABEL_DEFAULT;
    setting.maxTextLength = CONFIG_LINE_SIZE - 1;
    setting.inputTextBuffer = inputTextBuffer;
    setting.title = title;

    // Use the same IME init/status/result sequence as ItemzFlow.
    (void)sceImeDialogInit(&setting, NULL);

    int status = ORBIS_DIALOG_STATUS_NONE;
    for (;;)
    {
        status = sceImeDialogGetStatus();

        // ItemzFlow treats NONE as an early exit and FINISHED as completion.
        // OpenOrbis names the corresponding terminal state STOPPED.
        if (status == ORBIS_DIALOG_STATUS_STOPPED)
            break;

        if (status == ORBIS_DIALOG_STATUS_NONE)
        {
            sceImeDialogTerm();
            snprintf(gStatus, sizeof(gStatus), "PS4 keyboard did not start");
            return false;
        }

        sceKernelUsleep(10000);
    }

    OrbisDialogResult result = {};
    int resultCode = sceImeDialogGetResult(&result);
    sceImeDialogTerm();

    if (resultCode < 0 || result.endstatus != ORBIS_DIALOG_OK)
    {
        snprintf(gStatus, sizeof(gStatus), "Edit cancelled");
        return false;
    }

    char edited[CONFIG_LINE_SIZE] = {};
    size_t converted = wcstombs(edited, inputTextBuffer, sizeof(edited) - 1);
    if (converted == (size_t)-1)
    {
        snprintf(gStatus, sizeof(gStatus), "Could not read text from PS4 keyboard");
        return false;
    }
    edited[converted] = '\0';

    while (converted && (edited[converted - 1] == '\n' || edited[converted - 1] == '\r'))
        edited[--converted] = '\0';

    if (edited[0] == '\0')
    {
        snprintf(gStatus, sizeof(gStatus), "Empty line not saved; use TRIANGLE to delete");
        return false;
    }

    char previous[CONFIG_LINE_SIZE];
    CopyText(previous, sizeof(previous), gConfigLines[gIniCursor]);
    snprintf(gConfigLines[gIniCursor], CONFIG_LINE_SIZE, "%s\n", edited);
    if (!SaveConfigLinesSafely(false))
    {
        CopyText(gConfigLines[gIniCursor], CONFIG_LINE_SIZE, previous);
        LoadConfigLines();
        return false;
    }

    LoadConfigLines();
    LoadConfigEntries();
    if (gIniCursor >= gConfigLineCount)
        gIniCursor = gConfigLineCount > 0 ? gConfigLineCount - 1 : 0;

    snprintf(gStatus, sizeof(gStatus), "Line %d updated", gIniCursor + 1);
    return true;
}
