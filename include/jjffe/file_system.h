#ifndef JJFFE_FILE_SYSTEM_H
#define JJFFE_FILE_SYSTEM_H

#include <cstdint>
#include <cstdio>

namespace jjffe {

struct FileInfo {
    char name[16];
    int type;
    int size;
};

// File system functions
void FileBuildName(const char* filename, char* outPath);
void FileBuildInfoBlk(FileInfo* info, const char* name);
int FileFindFirst(FileInfo* info);
int FileFindNext(FileInfo* info);
int FileIsDir(const FileInfo* info);
int FileDelete(const char* filename);
int32_t FUNC_001456_FileDelete(const char* path, uint32_t* err);
void DirNavigateTree(const FileInfo* info);
char* DirGetCmmdrPath();
int FileGetNumDrives();
void FileWriteDrivesString(char* buffer, int bufferSize);
void ExtractDriveDir(const char* path, char* outDrive, char* outDir);
void InitDrivesStringW(uint16_t* buffer);

} // namespace jjffe

#endif // JJFFE_FILE_SYSTEM_H
