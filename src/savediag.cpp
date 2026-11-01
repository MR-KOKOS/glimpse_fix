// Split/Second PC fix - save game monitor (Log=1 only)
//
// The Steam build is known to break profiles around episode 6 ("save file is corrupted"). Reading
// and writing the .sav files (0x590C50, 0x590E90) runs inside SecuROM's VM. A hook on the save
// interface's vtable in .rdata (0xBA4820) coincided with failing integrity checks, so the game is
// left alone: the profile files are only watched from outside and every change is logged. The files are the
// plaintext profile XORed with a fixed key stream, so a logged file can be decrypted afterwards.
#include "common.h"
#include <shlobj.h>
#include <stdio.h>

struct SaveFile {
    char name[MAX_PATH];
    DWORD size;
    FILETIME written;
};
static SaveFile s_files[16];
static int s_count;

void WatchSaveFiles()
{
    static DWORD last;
    static char pattern[MAX_PATH];
    DWORD now = GetTickCount();
    if (now - last < 2000) return;
    last = now;
    if (!pattern[0]) {
        if (FAILED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, pattern))) return;
        strcat_s(pattern, "\\Disney Interactive Studios\\Split Second\\*.sav");
    }
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        int i = 0;
        while (i < s_count && strcmp(s_files[i].name, fd.cFileName)) i++;
        bool known = i < s_count;
        if (!known) {
            if (s_count == 16) continue;
            s_count++;
            strcpy_s(s_files[i].name, fd.cFileName);
        }
        SaveFile& f = s_files[i];
        if (known && f.size == fd.nFileSizeLow && !CompareFileTime(&f.written, &fd.ftLastWriteTime)) continue;
        f.size = fd.nFileSizeLow;
        f.written = fd.ftLastWriteTime;
        SYSTEMTIME st;
        FileTimeToSystemTime(&fd.ftLastWriteTime, &st);
        Log("save file %s %s: %u bytes, written %02u:%02u:%02u UTC", fd.cFileName, known ? "changed" : "found",
            f.size, st.wHour, st.wMinute, st.wSecond);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
