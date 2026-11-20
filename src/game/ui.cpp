#include "jjffe/ui.h"
#include "jjffe/draw.h"
#include "jjffe/string.h"
#include "jjffe/cdr_file.h"
#include "jjffe/game_state.h"
#include "jjffe/core/gameplay.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <sys/types.h>

namespace jjffe {

// Global menu state
MenuState g_menuState = {};

// Global console buttons
ConsoleButton g_consoleButtons[MAX_CONSOLE_BUTTONS] = {};

// Initialize menu system
void SysMenuInit()
{
    g_menuState.dialogType = DialogType::None;
    g_menuState.fileCount = 0;
    g_menuState.selectedIndex = -1;
    g_menuState.isActive = false;
    std::memset(g_menuState.currentPath, 0, sizeof(g_menuState.currentPath));
    std::memset(g_menuState.files, 0, sizeof(g_menuState.files));
}

// Cleanup menu system
void SysMenuCleanup()
{
    g_menuState.dialogType = DialogType::None;
    g_menuState.isActive = false;
}

// Draw file dialog based on current type
// FUNC_000191_SysMenuDrawFileDialog
void SysMenuDrawFileDialog()
{
    switch (g_menuState.dialogType) {
        case DialogType::Load:
            SysMenuDrawLoadDialog();
            break;
        case DialogType::Save:
            SysMenuDrawSaveDialog();
            break;
        case DialogType::Delete:
            SysMenuDrawDeleteDialog();
            break;
        default:
            break;
    }
}

// Draw load dialog
// FUNC_000193_SysMenuDrawLoadDialog
void SysMenuDrawLoadDialog()
{
    // Clear upper buffer
    DrawRect(0, 0, 320, 100, 0);
    
    // Draw title
    SysMenuWriteDialogString(0x940B, "LOAD GAME");
    
    // Draw file list
    SysMenuDrawFiles();
    
    // Draw buttons
    DrawBox(10, 180, 100, 195, 0x1F);
    DrawString("LOAD", 0x1F, 30, 185);
    
    DrawBox(120, 180, 210, 195, 0x1F);
    DrawString("CANCEL", 0x1F, 135, 185);
}

// Draw save dialog
// FUNC_000195_SysMenuDrawSaveDialog
void SysMenuDrawSaveDialog()
{
    // Clear upper buffer
    DrawRect(0, 0, 320, 100, 0);
    
    // Draw title
    SysMenuWriteDialogString(0x940C, "SAVE GAME");
    
    // Draw file list
    SysMenuDrawFiles();
    
    // Draw buttons
    DrawBox(10, 180, 100, 195, 0x1F);
    DrawString("SAVE", 0x1F, 30, 185);
    
    DrawBox(120, 180, 210, 195, 0x1F);
    DrawString("CANCEL", 0x1F, 135, 185);
}

// Draw delete dialog
// FUNC_000197_SysMenuDrawDeleteDialog
void SysMenuDrawDeleteDialog()
{
    // Clear upper buffer
    DrawRect(0, 0, 320, 100, 0);
    
    // Draw title
    SysMenuWriteDialogString(0x940D, "DELETE FILE");
    
    // Draw file list
    SysMenuDrawFiles();
    
    // Draw buttons
    DrawBox(10, 180, 100, 195, 0x1F);
    DrawString("DELETE", 0x1F, 25, 185);
    
    DrawBox(120, 180, 210, 195, 0x1F);
    DrawString("CANCEL", 0x1F, 135, 185);
}

// Read directory contents
// FUNC_000203_SysMenuReadDirectory
void SysMenuReadDirectory()
{
    // 1. Clear existing file list
    g_menuState.fileCount = 0;
    g_menuState.selectedIndex = -1;

    // 2. Use POSIX opendir/readdir to scan g_menuState.currentPath
    const char* scanPath = (g_menuState.currentPath[0] != '\0') ? g_menuState.currentPath : ".";
    DIR* dir = opendir(scanPath);
    if (!dir) return;  // Directory not accessible — leave list empty

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr && g_menuState.fileCount < 64) {
        const char* name = entry->d_name;
        size_t nameLen = strlen(name);

        // Skip non-.cdr and non-.jff files
        bool isCdr = (nameLen > 4 && strcmp(name + nameLen - 4, ".cdr") == 0);
        bool isJff = (nameLen > 4 && strcmp(name + nameLen - 4, ".jff") == 0);
        if (!isCdr && !isJff) continue;
        if (entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN) continue;

        // 3. Try to read save file header for metadata
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", scanPath, name);

        SaveFileHeader header = {};
        int errorCode = CdrFile::ERR_NONE;
        bool loaded = CdrFile::LoadGame(fullPath, &header, &errorCode);

        // 4. Create FileEntry with metadata
        FileEntry fileEntry = {};

        // Store on-disk filename for load/save/delete operations
        strncpy(fileEntry.fileName, name, sizeof(fileEntry.fileName) - 1);
        fileEntry.fileName[sizeof(fileEntry.fileName) - 1] = '\0';

        if (loaded && header.magic == SAVE_FILE_MAGIC) {
            // Format display name: "COMMANDER_NAME  MM/DD HH:MM"
            time_t t = static_cast<time_t>(header.timestamp);
            struct tm* tm_info = localtime(&t);
            char dateStr[16];
            strftime(dateStr, sizeof(dateStr), "%m/%d %H:%M", tm_info);

            // Use commander name from header (truncated to fit 24-char FileEntry)
            char displayName[24];
            snprintf(displayName, sizeof(displayName), "%.16s %.6s",
                     header.commanderName, dateStr);
            strncpy(fileEntry.name, displayName, sizeof(fileEntry.name) - 1);
            fileEntry.name[sizeof(fileEntry.name) - 1] = '\0';

            fileEntry.type = 0;  // Regular save file
            fileEntry.size = static_cast<int32_t>(header.payloadSize);
        } else {
            // Corrupted or unreadable — show filename as-is
            strncpy(fileEntry.name, name, sizeof(fileEntry.name) - 1);
            fileEntry.name[sizeof(fileEntry.name) - 1] = '\0';
            fileEntry.type = 0;
            fileEntry.size = 0;
        }

        SysMenuAddFile(&fileEntry);
    }

    closedir(dir);
}

// Add file to list
// FUNC_000199_SysMenuAddFile
void SysMenuAddFile(const FileEntry* file)
{
    if (g_menuState.fileCount < 64) {
        g_menuState.files[g_menuState.fileCount] = *file;
        g_menuState.fileCount++;
    }
}

// Draw file list
// FUNC_000198_SysMenuDrawFiles
void SysMenuDrawFiles()
{
    int x = 10;
    int y = 30;
    
    for (int i = 0; i < g_menuState.fileCount; i++) {
        const FileEntry& file = g_menuState.files[i];
        
        // Determine color based on type
        uint8_t color;
        if (file.type == 0) {
            color = 0x10;  // File
        } else if (file.type == 2) {
            color = 0x5F;  // Directory
        } else if (file.type == 4) {
            color = 0x11;  // Parent directory
        } else {
            color = 0x10;  // Default
        }
        
        // Highlight selected item
        if (i == g_menuState.selectedIndex) {
            DrawRect(x - 2, y - 2, 60, 10, 0x1F);
        }
        
        // Draw file name
        DrawString(file.name, color, x, y);
        
        // Move to next position
        y += 10;
        if (y > 150) {
            x += 62;
            y = 30;
        }
    }
}

// Change directory
// FUNC_000190_SysMenuChangeDir
void SysMenuChangeDir(const char* path)
{
    StringCopyN(g_menuState.currentPath, path, sizeof(g_menuState.currentPath));
    SysMenuReadDirectory();
}

// Load CDR file
// FUNC_000185_SysMenuLoadCdrFile
bool SysMenuLoadCdrFile(const char* filename)
{
    // 1. Construct full path: g_menuState.currentPath + "/" + filename
    char fullPath[512];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", g_menuState.currentPath, filename);

    // 2. Call CdrFile::LoadGame
    SaveFileHeader header = {};
    int errorCode = CdrFile::ERR_NONE;
    bool ok = CdrFile::LoadGame(fullPath, &header, &errorCode);

    // 3. On failure: display error, return false
    if (!ok) {
        CdrFile::DispErrorCode(errorCode);
        return false;
    }

    // 4. On success: set LastLoaded state
    CdrFile::SetLastLoaded(reinterpret_cast<const uint32_t*>(&header.gameProgress));

    // 5. Restore commander name
    SysMenuUpdateCmmdrName(header.commanderName);

    // 6. Close menu dialog
    SysMenuCleanup();

    return true;
}

// Save CDR file
// FUNC_000210_SysMenuSaveCdrFile
bool SysMenuSaveCdrFile(const char* filename)
{
    // 1. Construct full path
    char fullPath[512];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", g_menuState.currentPath, filename);

    // 2. Build SaveFileHeader
    SaveFileHeader header = {};
    header.magic = SAVE_FILE_MAGIC;
    header.version = CURRENT_SAVE_VERSION;
    header.flags = 0;
    header.timestamp = static_cast<uint64_t>(time(nullptr));

    // 3. Copy commander name (from game state or menu)
    snprintf(header.commanderName, sizeof(header.commanderName), "COMMANDER");

    // 4. Set game progress (frame count or mission number)
    header.gameProgress = static_cast<uint32_t>(g_gameState.frameCount);

    // 5. Call CdrFile::SaveGame
    int errorCode = CdrFile::ERR_NONE;
    bool ok = CdrFile::SaveGame(fullPath, header, &errorCode);

    // 6. On failure: display error
    if (!ok) {
        CdrFile::DispErrorCode(errorCode);
        return false;
    }

    // 7. Increment file extension for next save (00→01→02...)
    CdrFile::IncFileExtension();

    // 8. Update save name display
    SysMenuUpdateSaveName(filename);

    // 9. Refresh directory listing
    SysMenuReadDirectory();

    return true;
}

// Delete file
// FUNC_000187_SysMenuDeleteFile
bool SysMenuDeleteFile(const char* filename)
{
    // 1. Construct full path
    char fullPath[512];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", g_menuState.currentPath, filename);

    // 2. Call CdrFile::DeleteInternal
    int errorCode = CdrFile::ERR_NONE;
    bool ok = CdrFile::DeleteInternal(fullPath, &errorCode);

    // 3. On failure: display error
    if (!ok) {
        CdrFile::DispErrorCode(errorCode);
        return false;
    }

    // 4. Refresh directory listing
    SysMenuReadDirectory();

    return true;
}

// Process file selection
// FUNC_000205_SysMenuProcessFileSelect
void SysMenuProcessFileSelect(int32_t fileIndex)
{
    if (fileIndex >= 0 && fileIndex < g_menuState.fileCount) {
        g_menuState.selectedIndex = fileIndex;

        // Use on-disk filename (not display name) for file operations
        const char* actualFilename = g_menuState.files[fileIndex].fileName;

        // Handle different dialog types
        if (g_menuState.dialogType == DialogType::Load) {
            SysMenuLoadCdrFile(actualFilename);
        } else if (g_menuState.dialogType == DialogType::Save) {
            SysMenuSaveCdrFile(actualFilename);
        } else if (g_menuState.dialogType == DialogType::Delete) {
            SysMenuDeleteFile(actualFilename);
        }
    }
}

// Process dialog action
// FUNC_000204_SysMenuProcessFileDialogAction
void SysMenuProcessFileDialogAction()
{
    if (g_menuState.selectedIndex >= 0) {
        SysMenuProcessFileSelect(g_menuState.selectedIndex);
    }
}

// FUNC_000186_SysMenuUpdateSaveName
void FUNC_000186_SysMenuUpdateSaveName(const char* name);
void SysMenuUpdateSaveName(const char* name)
{
    FUNC_000186_SysMenuUpdateSaveName(name);
}

// Process if menu is on
// FUNC_000206_SysMenuProcessIfOn
void SysMenuProcessIfOn()
{
    if (g_menuState.isActive) {
        SysMenuProcessAction(0);
    }
}

// Process action event
// FUNC_000216_SysMenuProcessAction_Event4
void SysMenuProcessAction(int32_t eventCode)
{
    // Handle different events
    switch (eventCode) {
        case 0:
            // Default action
            break;
        case 1:
            // Cancel
            SysMenuCleanup();
            break;
        case 2:
            // Confirm
            SysMenuProcessFileDialogAction();
            break;
        default:
            break;
    }
}

// Write dialog string
// FUNC_000179_SysMenuWriteDialogString
void SysMenuWriteDialogString(int32_t stringId, const char* text)
{
    // Draw string at fixed position with shadow
    DrawShadowedChar(text[0], 0x11, 6, 9, 0, 200);
    if (text[1]) {
        DrawString(text + 1, 0x11, 12, 9);
    }
}

// Console button functions (Phase 15 / Phase 2 ASM parity)

// FUNC_000261_ConsoleDrawButtons — forward to ASM-table implementation in core
void ConsoleDrawButtons()
{
    FUNC_000261_ConsoleDrawButtons();
}

// FUNC_000262_ConsoleSetButtonImage — keep g_consoleButtons + DATA_CONBUTTON in sync
void ConsoleSetButtonImage(int buttonIndex, int imageIndex)
{
    if (buttonIndex < 0 || buttonIndex >= MAX_CONSOLE_BUTTONS) return;
    
    ConsoleButton& btn = g_consoleButtons[buttonIndex];
    
    if (btn.imageIndex != imageIndex) {
        // If setting to non-zero, clear same image from other buttons
        if (imageIndex != 0) {
            for (int i = 0; i < MAX_CONSOLE_BUTTONS; i++) {
                if (i != buttonIndex && g_consoleButtons[i].imageIndex == imageIndex) {
                    g_consoleButtons[i].imageIndex = 0;
                }
            }
        }
        btn.imageIndex = static_cast<int16_t>(imageIndex);
    }
    FUNC_000262_ConsoleSetButtonImage(buttonIndex, imageIndex);
}

// FUNC_000263_ConsoleShowButton
void ConsoleShowButton(int buttonIndex)
{
    if (buttonIndex < 0 || buttonIndex >= MAX_CONSOLE_BUTTONS) return;
    
    ConsoleButton& btn = g_consoleButtons[buttonIndex];
    if (!(btn.flags & 0x01)) {
        btn.flags |= 0x01;
    }
    FUNC_000263_ConsoleShowButton(buttonIndex);
}

// FUNC_000264_ConsoleHideButton
void ConsoleHideButton(int buttonIndex)
{
    if (buttonIndex < 0 || buttonIndex >= MAX_CONSOLE_BUTTONS) return;
    
    ConsoleButton& btn = g_consoleButtons[buttonIndex];
    if (btn.flags & 0x01) {
        btn.flags &= ~0x01;
    }
    FUNC_000264_ConsoleHideButton(buttonIndex);
}

// Helper: Check if button is visible
bool ConsoleIsButtonVisible(int buttonIndex)
{
    if (buttonIndex < 0 || buttonIndex >= MAX_CONSOLE_BUTTONS) return false;
    return (g_consoleButtons[buttonIndex].flags & 0x01) != 0;
}

// Helper: Get button image index
int ConsoleGetButtonImage(int buttonIndex)
{
    if (buttonIndex < 0 || buttonIndex >= MAX_CONSOLE_BUTTONS) return -1;
    return g_consoleButtons[buttonIndex].imageIndex;
}

// Phase 17B: SysMenu completion

// FUNC_000192_SysMenuSelectLoadDialog
void SysMenuSelectLoadDialog()
{
    g_menuState.dialogType = DialogType::Load;
    g_menuState.isActive = true;
    SysMenuReadDirectory();
}

// FUNC_000194_SysMenuSelectSaveDialog
void SysMenuSelectSaveDialog()
{
    g_menuState.dialogType = DialogType::Save;
    g_menuState.isActive = true;
    SysMenuReadDirectory();
}

// FUNC_000196_SysMenuSelectDeleteDialog
void SysMenuSelectDeleteDialog()
{
    g_menuState.dialogType = DialogType::Delete;
    g_menuState.isActive = true;
    SysMenuReadDirectory();
}

// FUNC_000201_SysMenuReadDirectoryInternal
void SysMenuReadDirectoryInternal()
{
    // Stub: Would iterate through directory entries using DirFindFirst/DirFindNext
    // For now, just call the public version
    SysMenuReadDirectory();
}

// FUNC_000212_SysMenuFileNameEntry / FUNC_000213_SysMenuCmmdrNameEntry take no
// argument in the ASM (they edit DATA_009117 / DATA_009121).
void FUNC_000212_SysMenuFileNameEntry(void);
void SysMenuFileNameEntry(const char* name)
{
    (void)name;
    FUNC_000212_SysMenuFileNameEntry();
}

void FUNC_000213_SysMenuCmmdrNameEntry(void);
void SysMenuCmmdrNameEntry(const char* name)
{
    (void)name;
    FUNC_000213_SysMenuCmmdrNameEntry();
}

// FUNC_000214_SysMenuZeroUnusedObjects
void FUNC_000214_SysMenuZeroUnusedObjects(void);
void SysMenuZeroUnusedObjects()
{
    FUNC_000214_SysMenuZeroUnusedObjects();
}

// FUNC_000215_SysMenuDisableSong
void FUNC_000215_SysMenuDisableSong(int32_t param_1);
void SysMenuDisableSong(int32_t songIndex)
{
    FUNC_000215_SysMenuDisableSong(songIndex);
}

int32_t FUNC_000217_SysMenuPlayTime(void);
void FUNC_000221_SysMenuLoadLastHotkey(void);
void FUNC_000222_SysMenuLoadDialogHotkey(void);
void FUNC_000916(void);

// FUNC_000217_SysMenuPlayTime (core.cpp)
void SysMenuPlayTime()
{
    FUNC_000217_SysMenuPlayTime();
}

// FUNC_000221_SysMenuLoadLastHotkey (core.cpp)
void SysMenuLoadLastHotkey()
{
    FUNC_000221_SysMenuLoadLastHotkey();
}

// FUNC_000222_SysMenuLoadDialogHotkey (core.cpp)
void SysMenuLoadDialogHotkey()
{
    FUNC_000222_SysMenuLoadDialogHotkey();
}

void FUNC_000225_SysMenuDisplayNonFile(void);

// FUNC_000225_SysMenuDisplayNonFile (core.cpp)
void SysMenuDisplayNonFile()
{
    FUNC_000225_SysMenuDisplayNonFile();
}

// FUNC_000226_SysMenuExpandString
void SysMenuExpandString(char* buffer, const char* format)
{
    // Stub: Would expand string with variables
    if (buffer && format) {
        // Simple copy for now
        size_t i = 0;
        while (format[i] && i < 255) {
            buffer[i] = format[i];
            i++;
        }
        buffer[i] = '\0';
    }
}

// FUNC_000230_SysMenuNewGame (ffe.asm:18749-18755): FUNC_000916, back to the title loop
void SysMenuNewGame()
{
    FUNC_000916();
}

} // namespace jjffe
