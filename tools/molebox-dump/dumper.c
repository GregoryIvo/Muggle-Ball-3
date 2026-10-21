/* Proxy bass.dll: forwards BASS_* to bassorig.dll and, after the game has
   initialised MoleBox, walks the virtual file system through the game's own
   hooked kernel32 imports and copies every file to C:\MB3OUT. */
#include <windows.h>
#include <stdio.h>

typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
typedef BOOL (WINAPI *ReadFile_t)(HANDLE,LPVOID,DWORD,LPDWORD,LPOVERLAPPED);
typedef DWORD (WINAPI *GetFileSize_t)(HANDLE,LPDWORD);
typedef BOOL (WINAPI *CloseHandle_t)(HANDLE);
typedef HANDLE (WINAPI *FindFirstFileA_t)(LPCSTR,LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *FindNextFileA_t)(HANDLE,LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI *FindClose_t)(HANDLE);

/* IAT slots of game32.img (image base 0x400000) */
#define SLOT(a) (*(void **)(a))
static CreateFileA_t hCreate; static ReadFile_t hRead; static GetFileSize_t hSize;
static CloseHandle_t hClose; static FindFirstFileA_t hFFirst; static FindNextFileA_t hFNext; static FindClose_t hFClose;
static FILE *logf; static int nfiles, nfail;

static void mkdirs(const char *path) {
    char tmp[MAX_PATH]; strcpy(tmp, path);
    for (char *p = tmp + 3; *p; p++) if (*p == '\\') { *p = 0; CreateDirectoryA(tmp, NULL); *p = '\\'; }
}

static int dump_one(const char *vpath) {
    HANDLE h = hCreate(vpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { fprintf(logf, "OPENFAIL %s\n", vpath); nfail++; return 0; }
    DWORD sz = hSize(h, NULL), got = 0;
    char *buf = HeapAlloc(GetProcessHeap(), 0, sz ? sz : 1);
    BOOL ok = hRead(h, buf, sz, &got, NULL);
    hClose(h);
    char out[MAX_PATH]; snprintf(out, sizeof out, "C:\\MB3OUT\\%s", vpath);
    mkdirs(out);
    FILE *f = fopen(out, "wb"); if (f) { fwrite(buf, 1, got, f); fclose(f); }
    fprintf(logf, "%s %s %lu %lu\n", ok && got == sz ? "OK" : "SHORT", vpath, sz, got);
    HeapFree(GetProcessHeap(), 0, buf); nfiles++;
    return 1;
}

static void walk(const char *dir) {
    char pat[MAX_PATH]; WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    HANDLE f = hFFirst(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) { fprintf(logf, "NODIR %s\n", dir); return; }
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) walk(p); else dump_one(p);
    } while (hFNext(f, &fd));
    hFClose(f);
}

static DWORD WINAPI worker(LPVOID arg) {
    Sleep(20000);
    CreateDirectoryA("C:\\MB3OUT", NULL);
    logf = fopen("C:\\MB3OUT\\_dump.log", "w");
    hFFirst = SLOT(0x4f0110); hFClose = SLOT(0x4f0114); hFNext = SLOT(0x4f0118);
    hSize = SLOT(0x4f0158); hCreate = SLOT(0x4f0164); hClose = SLOT(0x4f0170); hRead = SLOT(0x4f0188);
    fprintf(logf, "hooks %p %p %p %p %p %p %p\n", hFFirst, hFNext, hFClose, hCreate, hRead, hSize, hClose);
    walk("Data");
    /* names seen in the MoleBox table, in case directory enumeration misses some */
    FILE *lst = fopen("C:\\MB3\\mbfiles.txt", "r");
    if (lst) { char line[MAX_PATH];
        while (fgets(line, sizeof line, lst)) { line[strcspn(line, "\r\n")] = 0;
            char out[MAX_PATH]; snprintf(out, sizeof out, "C:\\MB3OUT\\%s", line);
            if (GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES) dump_one(line); }
        fclose(lst); }
    fprintf(logf, "DONE files=%d fail=%d\n", nfiles, nfail); fclose(logf);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    if (reason == DLL_PROCESS_ATTACH) CreateThread(NULL, 0, worker, NULL, 0, NULL);
    return TRUE;
}
