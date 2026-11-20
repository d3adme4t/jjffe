#include "jjffe/file_system.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>

struct CFileInfo {
    char pName[16];
    int type;
    int size;
};

namespace jjffe {
static char g_fileCurrentPath[256] = "commandr/";
}

// linux/linuxdir.c DirFindFirst / DirFindNext over the commander path: the
// parent entry "(Parent Dir)/" (type 4) unless at "/", then the directories
// (type 2, '/' appended), then the files (type 0, size from the file); names
// starting with '.' are skipped.
static DIR* s_dirHandle = nullptr;
static int s_dirFirst = 1;
static int s_dirDirs = 1;

static void DirGetFileInfo(CFileInfo* info)
{
    char path[512];
    std::snprintf(path, sizeof path, "%s%s", jjffe::g_fileCurrentPath, info->pName);
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        info->type = 2;
        info->size = 0;
    } else {
        info->type = 0;
        info->size = stat(path, &st) == 0 ? static_cast<int>(st.st_size) : 0;
    }
}

extern "C" int DirFindNext(CFileInfo* pFile)
{
    for (;;) {
        if (s_dirFirst) {
            s_dirHandle = opendir(jjffe::g_fileCurrentPath);
            s_dirFirst = 0;
        }
        const struct dirent* d = s_dirHandle ? readdir(s_dirHandle) : nullptr;
        if (!d) {
            if (s_dirHandle) closedir(s_dirHandle);
            s_dirHandle = nullptr;
            if (!s_dirDirs) return 0;
            s_dirDirs = 0;
            s_dirFirst = 1;
            continue;
        }
        std::strncpy(pFile->pName, d->d_name, 15);
        pFile->pName[15] = 0;
        DirGetFileInfo(pFile);
        if (s_dirDirs && pFile->type == 0) continue;
        if (!s_dirDirs && pFile->type == 2) continue;
        if (pFile->pName[0] == '.') continue;
        break;
    }
    if (pFile->type == 2 && std::strlen(pFile->pName) < 15) std::strcat(pFile->pName, "/");
    return 1;
}

extern "C" int DirFindFirst(CFileInfo* pFile)
{
    if (s_dirHandle) closedir(s_dirHandle);
    s_dirHandle = nullptr;
    s_dirFirst = 1;
    s_dirDirs = 1;
    if (std::strcmp(jjffe::g_fileCurrentPath, "/") == 0) return DirFindNext(pFile);
    std::strcpy(pFile->pName, "(Parent Dir)/");
    pFile->type = 4;
    pFile->size = 0;
    return 1;
}

namespace jjffe {

static int g_fileNumDrives = 1;

// FUNC_001450_FileBuildName
void FileBuildName(const char* filename, char* outPath)
{
    strcpy(outPath, g_fileCurrentPath);
    strcat(outPath, filename);
}

// FUNC_001451_FileBuildInfoBlk
void FileBuildInfoBlk(FileInfo* info, const char* name)
{
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    
    char fullPath[512];
    FileBuildName(name, fullPath);
    
    struct stat st;
    if (stat(fullPath, &st) == 0) {
        info->size = (int)st.st_size;
        info->type = S_ISDIR(st.st_mode) ? 2 : 0;
    } else {
        info->size = 0;
        info->type = 0;
    }
}

// FUNC_001452_FileFindFirst
int FileFindFirst(FileInfo* info)
{
    CFileInfo cInfo;
    int result = DirFindFirst(&cInfo);
    if (result) {
        strncpy(info->name, cInfo.pName, sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        info->type = cInfo.type;
        info->size = cInfo.size;
    }
    return result;
}

// FUNC_001453_FileFindNext
int FileFindNext(FileInfo* info)
{
    CFileInfo cInfo;
    int result = DirFindNext(&cInfo);
    if (result) {
        strncpy(info->name, cInfo.pName, sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        info->type = cInfo.type;
        info->size = cInfo.size;
    }
    return result;
}

// FUNC_001455_FileIsDir
int FileIsDir(const FileInfo* info)
{
    return (info->type == 2 || info->type == 4) ? 1 : 0;
}

// FUNC_001456_FileDelete (ffe.asm:86741-86766): remove(path); 1 if it worked,
// else 0. *err is 0 either way (the errno read is commented out in the ASM).
int32_t FUNC_001456_FileDelete(const char* path, uint32_t* err)
{
    const int32_t ok = remove(path) == 0 ? 1 : 0;
    *err = 0;
    return ok;
}

int FileDelete(const char* filename)
{
    char fullPath[512];
    FileBuildName(filename, fullPath);
    uint32_t err;
    return FUNC_001456_FileDelete(fullPath, &err);
}

// linux/linuxdir.c DirNavigateTree: a directory entry (type 2) appends its
// name; the parent entry (type 4) drops the last component.
void DirNavigateTree(const FileInfo* info)
{
    if (info->type == 2) strcat(g_fileCurrentPath, info->name);
    if (info->type == 4) {
        const size_t i = strlen(g_fileCurrentPath);
        g_fileCurrentPath[i - 1] = 0;
        char* const slash = strrchr(g_fileCurrentPath, '/');
        if (slash) slash[1] = 0;
        else g_fileCurrentPath[i - 1] = '/';
    }
}

char* DirGetCmmdrPath()
{
    return g_fileCurrentPath;
}

// FUNC_001459_FileGetNumDrives
int FileGetNumDrives()
{
    return g_fileNumDrives;
}

// FUNC_001460_FileWriteDrivesString
void FileWriteDrivesString(char* buffer, int bufferSize)
{
    snprintf(buffer, bufferSize, "C:");
}

// FUNC_001462_ExtractDriveDir
void ExtractDriveDir(const char* path, char* outDrive, char* outDir)
{
    if (path[0] != '\0' && path[1] == ':') {
        outDrive[0] = path[0];
        outDrive[1] = '\0';
        if (path[2] != '\0') {
            strcpy(outDir, path + 2);
        } else {
            outDir[0] = '\0';
        }
    } else {
        outDrive[0] = 'C';
        outDrive[1] = '\0';
        strcpy(outDir, path);
    }
}

// FUNC_001540_InitDrivesStringW
void InitDrivesStringW(uint16_t* buffer)
{
    buffer[0] = 'C';
    buffer[1] = ':';
    buffer[2] = 0;
}

} // namespace jjffe
