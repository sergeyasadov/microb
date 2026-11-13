#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "generator.h"
#include "logger.h"

#include <unistd.h>
#include <stdlib.h>

int
main(int, char**)
{
    char* cur_path = getcwd(NULL, 0);
    if(!cur_path)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Main", "Can't get the current directory");
        return EXIT_FAILURE;
    }

    struct config* cfg = mb_config_init(cur_path);

    int ok = cfg && mb_config_parse(&cfg);
    if(!ok) mb_log(MB_LOG_LEVEL_ERROR, "Main", "Makefile is not generated");

    ok = ok && mb_generator__makefile(cfg);

    mb_config_free(&cfg);

    free(cur_path);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
