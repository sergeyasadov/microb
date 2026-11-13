#ifndef __MICROB_PLATFORM_H__
#define __MICROB_PLATFORM_H__


#include <stddef.h>


char* mb_platform_cwd(void);

char* mb_platform_exe_path(void);

char* mb_platform_realpath(const char* path);

int mb_platform_mkdir(const char* path);

int mb_platform_is_dir(const char* path);
int mb_platform_is_file(const char* path);

char** mb_platform_list_dir(const char* path, size_t* count);

int mb_platform_replace_file(const char* from, const char* to);

int mb_platform_stderr_color(void);

#endif // __MICROB_PLATFORM_H__
