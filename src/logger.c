#include "logger.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


#define MB_COLOR_RED     "\x1B[31m"
#define MB_COLOR_GREEN   "\x1B[32m"
#define MB_COLOR_YELLOW  "\x1B[33m"
#define MB_COLOR_RESET   "\x1B[0m"
#define MB_BOLD          "\x1B[1m"


static inline
int
log_use_color(void)
{
    static int color = -1;
    if(color < 0)
    {
        const char* no_color = getenv("NO_COLOR");
        color = !(no_color && *no_color) && mb_platform_stderr_color();
    }
    return color;
}


void
mb_log(const enum log_level log_level, const char* module, const char* fmt, ...)
{
    const char* tag;
    const char* color;

    switch(log_level)
    {
        case MB_LOG_LEVEL_INFO:  tag = "I"; color = MB_COLOR_GREEN;  break;
        case MB_LOG_LEVEL_WARN:  tag = "W"; color = MB_COLOR_YELLOW; break;
        case MB_LOG_LEVEL_ERROR: tag = "E"; color = MB_COLOR_RED;    break;
        default: return;
    }

    if(log_use_color())
        fprintf(stderr, "[%s" MB_BOLD "%s" MB_COLOR_RESET "] [%12s] ", color, tag, module);
    else
        fprintf(stderr, "[%s] [%12s] ", tag, module);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
