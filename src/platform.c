#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static
char**
platform_names_add(char** names, size_t* count, const char* name)
{
    if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return names;

    names = (char**)mb_xrealloc(names, (*count + 1) * sizeof(char*));
    names[(*count)++] = mb_strdup(name);
    return names;
}


#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>


static
char*
platform_slashes(char* path)
{
    if(path)
        for(char* c = path; *c; c++)
            if(*c == '\\') *c = '/';
    return path;
}


char*
mb_platform_cwd(void)
{
    const DWORD size = GetCurrentDirectoryA(0, NULL);
    if(size == 0) return NULL;

    char* path = (char*)mb_xmalloc(size);
    if(GetCurrentDirectoryA(size, path) == 0)
    {
        free(path);
        return NULL;
    }
    return platform_slashes(path);
}


char*
mb_platform_exe_path(void)
{
    DWORD size = MAX_PATH;

    for(;;)
    {
        char* path = (char*)mb_xmalloc(size);
        const DWORD n = GetModuleFileNameA(NULL, path, size);

        if(n == 0)
        {
            free(path);
            return NULL;
        }
        if(n < size) return platform_slashes(path);

        free(path);
        size *= 2;
    }
}


char*
mb_platform_realpath(const char* path)
{
    if(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return NULL;

    char* full = _fullpath(NULL, path, 0);
    if(!full) return NULL;

    char* copy = mb_strdup(full);
    free(full);
    return platform_slashes(copy);
}


int
mb_platform_mkdir(const char* path)
{
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}


int
mb_platform_is_dir(const char* path)
{
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}


int
mb_platform_is_file(const char* path)
{
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}


char**
mb_platform_list_dir(const char* path, size_t* count)
{
    *count = 0;

    char* pattern = mb_path_join(path, "*");
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    free(pattern);

    if(h == INVALID_HANDLE_VALUE) return NULL;

    char** names = NULL;
    do
    {
        names = platform_names_add(names, count, data.cFileName);
    }
    while(FindNextFileA(h, &data));

    FindClose(h);
    return names;
}


int
mb_platform_replace_file(const char* from, const char* to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}


int
mb_platform_stderr_color(void)
{
    if(!_isatty(_fileno(stderr))) return 0;

    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    if(h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return 0;

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}


#else

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdint.h>
#endif


char*
mb_platform_cwd(void)
{
    return getcwd(NULL, 0);
}


char*
mb_platform_exe_path(void)
{
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);

    char* exe = (char*)mb_xmalloc(size + 1);
    char* real = _NSGetExecutablePath(exe, &size) == 0 ? realpath(exe, NULL) : NULL;
    free(exe);
    return real;
#elif defined(__linux__)
    return realpath("/proc/self/exe", NULL);
#elif defined(__FreeBSD__)
    return realpath("/proc/curproc/file", NULL);
#else
    return NULL;
#endif
}


char*
mb_platform_realpath(const char* path)
{
    return realpath(path, NULL);
}


int
mb_platform_mkdir(const char* path)
{
    return mkdir(path, 0777) == 0 || errno == EEXIST;
}


int
mb_platform_is_dir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


int
mb_platform_is_file(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


char**
mb_platform_list_dir(const char* path, size_t* count)
{
    *count = 0;

    DIR* d = opendir(path);
    if(!d) return NULL;

    char** names = NULL;
    struct dirent* e;
    while((e = readdir(d)) != NULL)
        names = platform_names_add(names, count, e->d_name);

    closedir(d);
    return names;
}


int
mb_platform_replace_file(const char* from, const char* to)
{
    return rename(from, to) == 0;
}


int
mb_platform_stderr_color(void)
{
    return isatty(fileno(stderr));
}

#endif
