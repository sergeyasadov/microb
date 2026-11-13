#define _POSIX_C_SOURCE 200809L

#include "mcu_db.h"
#include "logger.h"
#include "utils.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdint.h>
#endif


#define DB_PROJECT_ROOT "mcu"
#define DB_FAMILY_DIR "family"


static
char*
db_exe_dir(void)
{
    char* real = NULL;

#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);

    char* exe = (char*)mb_xmalloc(size + 1);
    if(_NSGetExecutablePath(exe, &size) == 0)
        real = realpath(exe, NULL);
    free(exe);
#elif defined(__linux__)
    real = realpath("/proc/self/exe", NULL);
#endif

    if(!real) return NULL;

    char* dir = mb_dirname(real);
    free(real);
    return dir;
}


static
int
db_same_dir(const char* a, const char* b)
{
    char* ra = realpath(a, NULL);
    char* rb = realpath(b, NULL);
    const int same = ra && rb && strcmp(ra, rb) == 0;
    free(ra);
    free(rb);
    return same;
}


void
mb_mcu_db_init(struct mcu_db* db)
{
    memset(db, 0, sizeof(*db));

    if(mb_is_dir(DB_PROJECT_ROOT))
        db->roots[db->nroots++] = mb_strdup(DB_PROJECT_ROOT);

    char* sys = NULL;
    const char* home = getenv("MICROB_HOME");

    if(home && *home)
    {
        sys = mb_path_join(home, "mcu");
    }
    else
    {
        char* dir = db_exe_dir();
        if(dir)
        {
            sys = mb_path_join(dir, "mcu");
            free(dir);
        }
        else
        {
            mb_log(MB_LOG_LEVEL_WARN, "MCU DB", "Can't find the microb executable path, set MICROB_HOME");
        }
    }

    if(!sys) return;

    if(!mb_is_dir(sys))
    {
        mb_log(MB_LOG_LEVEL_WARN, "MCU DB", "System MCU database '%s' not found", sys);
        free(sys);
    }
    else if(db->nroots > 0 && db_same_dir(db->roots[0], sys))
    {
        free(sys);
    }
    else
    {
        db->roots[db->nroots++] = sys;
    }

    for(size_t i = 0; i < db->nroots; i++)
        mb_log(MB_LOG_LEVEL_INFO, "MCU DB", "Database '%s'", db->roots[i]);
}


void
mb_mcu_db_free(struct mcu_db* db)
{
    for(size_t i = 0; i < db->nroots; i++) free(db->roots[i]);
    memset(db, 0, sizeof(*db));
}


char*
mb_mcu_db_find(const struct mcu_db* db, const char* rel)
{
    for(size_t i = 0; i < db->nroots; i++)
    {
        char* path = mb_path_join(db->roots[i], rel);
        if(mb_is_file(path)) return path;
        free(path);
    }
    return NULL;
}


static
int
db_cmp_str(const void* a, const void* b)
{
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}


static
char**
db_list_dirs(const char* root, size_t* count)
{
    *count = 0;

    DIR* d = opendir(root);
    if(!d) return NULL;

    char** names = NULL;
    struct dirent* e;

    while((e = readdir(d)) != NULL)
    {
        if(e->d_name[0] == '.' || strcmp(e->d_name, DB_FAMILY_DIR) == 0) continue;

        char* path = mb_path_join(root, e->d_name);
        const int dir = mb_is_dir(path);
        free(path);
        if(!dir) continue;

        names = (char**)mb_xrealloc(names, (*count + 1) * sizeof(char*));
        names[(*count)++] = mb_strdup(e->d_name);
    }

    closedir(d);

    if(*count > 1) qsort(names, *count, sizeof(char*), db_cmp_str);
    return names;
}


char*
mb_mcu_db_find_mcu(const struct mcu_db* db, const char* name, char** rel_dir)
{
    char* file = (char*)mb_xmalloc(strlen(name) + sizeof(".ini"));
    strcpy(file, name);
    strcat(file, ".ini");

    char* found = NULL;

    for(size_t r = 0; r < db->nroots && !found; r++)
    {
        size_t count = 0;
        char** dirs = db_list_dirs(db->roots[r], &count);

        for(size_t i = 0; i < count; i++)
        {
            if(!found)
            {
                char* dir = mb_path_join(db->roots[r], dirs[i]);
                char* path = mb_path_join(dir, file);
                free(dir);

                if(mb_is_file(path))
                {
                    found = path;
                    if(rel_dir) *rel_dir = mb_strdup(dirs[i]);
                }
                else
                {
                    free(path);
                }
            }
            free(dirs[i]);
        }
        free(dirs);
    }

    free(file);
    return found;
}


int
mb_mcu_db_valid_name(const char* name)
{
    if(*name == '\0' || *name == '.' || strstr(name, "..")) return 0;

    for(const char* c = name; *c; c++)
    {
        if(!(isalnum((unsigned char)*c) || *c == '_' || *c == '-' || *c == '.' || *c == '+')) return 0;
    }
    return 1;
}


char*
mb_mcu_db_roots_str(const struct mcu_db* db)
{
    struct mb_buf b;
    mb_buf_init(&b);

    for(size_t i = 0; i < db->nroots; i++)
        mb_buf_printf(&b, "%s'%s'", i ? ", " : "", db->roots[i]);

    if(db->nroots == 0) mb_buf_puts(&b, "none");

    char* s = mb_strdup(mb_buf_str(&b));
    mb_buf_free(&b);
    return s;
}
