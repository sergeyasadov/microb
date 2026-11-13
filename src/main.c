#include "config.h"
#include "generator.h"
#include "logger.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define MAIN_MAX_GENERATORS 8
#define MAIN_DEFAULT_GENERATOR "makefile"


static
void
main_usage(FILE* f)
{
    fprintf(f,
        "usage: microb [-g GENERATOR]...\n"
        "\n"
        "Generates build files from microb.ini in the current directory\n"
        "(the first run creates a microb.ini template).\n"
        "\n"
        "  -g, --generator NAME  build system to generate for, may be repeated;\n"
        "                        overrides 'generator' in microb.ini (default: " MAIN_DEFAULT_GENERATOR ")\n"
        "  -h, --help            show this help\n"
        "\n"
        "generators:\n");

    for(const struct mb_generator* const* g = mb_generator_all(); *g; g++)
        fprintf(f, "  %-10s -> %s\n", (*g)->name, (*g)->output);
}


static
int
main_add_generator(const struct mb_generator** list, size_t* n, const char* name)
{
    const struct mb_generator* g = mb_generator_find(name);
    if(!g)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Main", "Unknown generator '%s', see microb --help", name);
        return 0;
    }

    for(size_t i = 0; i < *n; i++)
        if(list[i] == g) return 1;

    if(*n == MAIN_MAX_GENERATORS)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Main", "Too many generators");
        return 0;
    }

    list[(*n)++] = g;
    return 1;
}


static
int
main_config_generators(const struct config* cfg, const struct mb_generator** list, size_t* n)
{
    const char* value = mb_config_get(cfg, "generator");
    if(!value) value = MAIN_DEFAULT_GENERATOR;

    char* copy = mb_strdup(value);
    int ok = 1;

    for(char* tok = strtok(copy, " \t,"); tok; tok = strtok(NULL, " \t,"))
        ok = main_add_generator(list, n, tok) && ok;

    free(copy);
    return ok;
}


int
main(int argc, char** argv)
{
    const struct mb_generator* generators[MAIN_MAX_GENERATORS];
    size_t ngenerators = 0;
    int ok = 1;

    for(int i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        const char* name = NULL;

        if(strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
        {
            main_usage(stdout);
            return EXIT_SUCCESS;
        }
        else if(strcmp(a, "-g") == 0 || strcmp(a, "--generator") == 0)
        {
            if(i + 1 == argc)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Main", "'%s' needs a generator name", a);
                return EXIT_FAILURE;
            }
            name = argv[++i];
        }
        else if(strncmp(a, "--generator=", 12) == 0)
        {
            name = a + 12;
        }
        else if(strncmp(a, "-g", 2) == 0)
        {
            name = a + 2;
        }
        else
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Main", "Unknown argument '%s'", a);
            main_usage(stderr);
            return EXIT_FAILURE;
        }

        ok = main_add_generator(generators, &ngenerators, name) && ok;
    }

    if(!ok) return EXIT_FAILURE;

    char* cur_path = mb_platform_cwd();
    if(!cur_path)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Main", "Can't get the current directory");
        return EXIT_FAILURE;
    }

    struct config* cfg = mb_config_init(cur_path);

    ok = cfg && mb_config_parse(&cfg);
    ok = ok && (ngenerators > 0 || main_config_generators(cfg, generators, &ngenerators));

    for(size_t i = 0; ok && i < ngenerators; i++)
        ok = generators[i]->generate(cfg);

    if(!ok) mb_log(MB_LOG_LEVEL_ERROR, "Main", "Build files are not generated");

    mb_config_free(&cfg);
    free(cur_path);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
