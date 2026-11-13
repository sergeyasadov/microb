#include "mcu_db.h"
#include "ini.h"
#include "logger.h"
#include "platform.h"
#include "utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>


#define DB_PROJECT_ROOT "mcu"
#define DB_FAMILY_DIR "family"


static
char*
db_exe_dir(void)
{
    char* exe = mb_platform_exe_path();
    if(!exe) return NULL;

    char* dir = mb_dirname(exe);
    free(exe);
    return dir;
}


static
int
db_same_dir(const char* a, const char* b)
{
    char* ra = mb_platform_realpath(a);
    char* rb = mb_platform_realpath(b);
    const int same = ra && rb && strcmp(ra, rb) == 0;
    free(ra);
    free(rb);
    return same;
}


static
char*
db_system_root(void)
{
    const char* home = getenv("MICROB_HOME");
    if(home && *home) return mb_path_join(home, "mcu");

    char* dir = db_exe_dir();
    if(!dir)
    {
        mb_log(MB_LOG_LEVEL_WARN, "MCU DB", "Can't find the microb executable path, set MICROB_HOME");
        return NULL;
    }

    static const char* const candidates[] = {"mcu", "../share/microb/mcu"};
    char* found = NULL;

    for(size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && !found; i++)
    {
        char* path = mb_path_join(dir, candidates[i]);
        if(mb_is_dir(path)) found = path;
        else free(path);
    }

    if(!found) found = mb_path_join(dir, candidates[0]);

    free(dir);
    return found;
}


void
mb_mcu_db_init(struct mcu_db* db)
{
    memset(db, 0, sizeof(*db));

    if(mb_is_dir(DB_PROJECT_ROOT))
        db->roots[db->nroots++] = mb_strdup(DB_PROJECT_ROOT);

    char* sys = db_system_root();

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
    size_t n = 0;
    char** names = mb_platform_list_dir(root, &n);
    *count = 0;

    for(size_t i = 0; i < n; i++)
    {
        char* path = mb_path_join(root, names[i]);
        const int keep = names[i][0] != '.' && strcmp(names[i], DB_FAMILY_DIR) != 0 && mb_is_dir(path);
        free(path);

        if(keep) names[(*count)++] = names[i];
        else free(names[i]);
    }

    if(*count == 0)
    {
        free(names);
        names = NULL;
    }

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


static
int
db_glob(const char* p, const char* s)
{
    for(; *p; p++, s++)
    {
        if(*p == '*')
        {
            for(const char* t = s;; t++)
            {
                if(db_glob(p + 1, t)) return 1;
                if(*t == '\0') return 0;
            }
        }
        if(*s == '\0') return 0;
        if(*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s)) return 0;
    }
    return *s == '\0';
}


static
const char*
db_ini_get(const struct ini* ini, const char* key)
{
    const char* value = NULL;
    for(size_t i = 0; i < ini->nentries; i++)
        if(ini->entries[i].section < 0 && ini->entries[i].value && strcmp(ini->entries[i].key, key) == 0)
            value = ini->entries[i].value;
    return value;
}


static
int
db_family_matches(const char* path, const char* name)
{
    struct ini ini;
    int found = 0;

    if(mb_ini_load(&ini, path))
    {
        const char* patterns = db_ini_get(&ini, "match");
        if(patterns)
        {
            size_t n = 0;
            char** v = mb_split_args(patterns, &n);
            for(size_t i = 0; i < n && !found; i++) found = db_glob(v[i], name);
            mb_strv_free(v, n);
        }
    }

    mb_ini_free(&ini);
    return found;
}


char*
mb_mcu_db_match_family(const struct mcu_db* db, const char* name, int* ambiguous)
{
    *ambiguous = 0;

    char** families = NULL;
    size_t nfamilies = 0;
    char** seen = NULL;
    size_t nseen = 0;

    for(size_t r = 0; r < db->nroots; r++)
    {
        char* dir = mb_path_join(db->roots[r], DB_FAMILY_DIR);
        size_t n = 0;
        char** files = mb_platform_list_dir(dir, &n);
        if(n > 1) qsort(files, n, sizeof(char*), db_cmp_str);

        for(size_t i = 0; i < n; i++)
        {
            const size_t len = strlen(files[i]);
            int dup = len <= 4 || strcmp(files[i] + len - 4, ".ini") != 0;
            for(size_t j = 0; j < nseen && !dup; j++) dup = strcmp(seen[j], files[i]) == 0;
            if(dup) continue;

            seen = (char**)mb_xrealloc(seen, (nseen + 1) * sizeof(char*));
            seen[nseen++] = mb_strdup(files[i]);

            char* path = mb_path_join(dir, files[i]);
            if(db_family_matches(path, name))
            {
                families = (char**)mb_xrealloc(families, (nfamilies + 1) * sizeof(char*));
                families[nfamilies++] = mb_strndup(files[i], len - 4);
            }
            free(path);
        }

        mb_strv_free(files, n);
        free(dir);
    }

    mb_strv_free(seen, nseen);

    char* family = NULL;
    if(nfamilies == 1)
    {
        family = families[0];
        families[0] = NULL;
    }
    else if(nfamilies > 1)
    {
        struct mb_buf list;
        mb_buf_init(&list);
        for(size_t i = 0; i < nfamilies; i++) mb_buf_printf(&list, "%s'%s'", i ? ", " : "", families[i]);
        mb_log(MB_LOG_LEVEL_ERROR, "MCU DB", "MCU '%s' matches several families (%s), set 'family' in microb.ini",
            name, mb_buf_str(&list));
        mb_buf_free(&list);
        *ambiguous = 1;
    }

    mb_strv_free(families, nfamilies);
    return family;
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
