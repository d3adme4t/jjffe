#ifndef JJFFE_UI_H
#define JJFFE_UI_H

#include <cstdint>

namespace jjffe {

// Dialog types
enum class DialogType : uint8_t {
    None = 0,
    Load = 3,
    Save = 4,
    Delete = 5
};

// File entry structure
struct FileEntry {
    char name[24];      // Display name (commander name + date for save files)
    char fileName[24];  // On-disk filename (e.g., "SAVE00.CDR")
    int32_t type;       // File type (0=file, 2=directory, 4=parent)
    int32_t size;       // File size
};

// Menu state structure
struct MenuState {
    DialogType dialogType;
    int32_t fileCount;
    FileEntry files[64];        // Max 64 files
    char currentPath[256];      // Current directory path
    int32_t selectedIndex;      // Currently selected file
    bool isActive;              // Menu is active
};

// Console button — C++ mirror used by tests / helpers. Runtime draw
// (FUNC_000261) reads ASM DATA_CONBUTTON; ConsoleSet/Show/Hide sync both.
struct ConsoleButton {
    uint8_t flags;          // Bit 0 = visible (ASM +0x30)
    uint8_t padding1;
    int16_t imageIndex;     // Current image index (ASM +0x34)
    int16_t defaultImage;   // Default image index
    int16_t x;              // X position (ASM +0x00)
    int16_t y;              // Y position (ASM +0x04)
    int16_t width;          // Button width (tests)
    int16_t height;         // Button height (tests)
    uint8_t data[46];       // Additional button data
};

// Constants
constexpr int MAX_CONSOLE_BUTTONS = 82;  // 0x52 buttons

// Global menu state
extern MenuState g_menuState;

// Global console buttons
extern ConsoleButton g_consoleButtons[MAX_CONSOLE_BUTTONS];

// Dialog functions
void SysMenuInit();
void SysMenuCleanup();
void SysMenuDrawFileDialog();
void SysMenuDrawLoadDialog();
void SysMenuDrawSaveDialog();
void SysMenuDrawDeleteDialog();
void SysMenuSelectLoadDialog();
void SysMenuSelectSaveDialog();
void SysMenuSelectDeleteDialog();

// File management
void SysMenuReadDirectory();
void SysMenuReadDirectoryInternal();
void SysMenuAddFile(const FileEntry* file);
void SysMenuDrawFiles();
void SysMenuChangeDir(const char* path);

// File operations
bool SysMenuLoadCdrFile(const char* filename);
bool SysMenuSaveCdrFile(const char* filename);
bool SysMenuDeleteFile(const char* filename);

// Selection and input
void SysMenuProcessFileSelect(int32_t fileIndex);
void SysMenuProcessFileDialogAction();
void SysMenuUpdateSaveName(const char* name);
void SysMenuFileNameEntry(const char* name);
void SysMenuCmmdrNameEntry(const char* name);
void SysMenuUpdateCmmdrName(const char* name);

// Event processing
void SysMenuProcessIfOn();
void SysMenuProcessAction(int32_t eventCode);

// Time and playback
void SysMenuPlayTime();

// Hotkeys
void SysMenuLoadLastHotkey();
void SysMenuLoadDialogHotkey();

// Game state
void SysMenuNewGame();
void SysMenuZeroUnusedObjects();
void SysMenuDisableSong(int32_t songIndex);

// Display and UI
void SysMenuDisplayNonFile();
void SysMenuExpandString(char* buffer, const char* format);

// Module communication

// Display helpers
void SysMenuWriteDialogString(int32_t stringId, const char* text);

// Console button functions (Phase 15)
void ConsoleDrawButtons();
void ConsoleSetButtonImage(int buttonIndex, int imageIndex);
void ConsoleShowButton(int buttonIndex);
void ConsoleHideButton(int buttonIndex);
bool ConsoleIsButtonVisible(int buttonIndex);
int ConsoleGetButtonImage(int buttonIndex);

} // namespace jjffe

#endif // JJFFE_UI_H
