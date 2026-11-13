#include "ini.h"
#include "logger.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void
mb_ini_init(struct ini* ini, const char* path)
{
    memset(ini, 0, sizeof(*ini));
    ini->path = mb_strdup(path);
}


int
mb_ini_normalize_key(char* key)
{
    if(*key == '\0') return 0;

    for(char* c = key; *c; c++)
    {
        *c = (char)tolower((unsigned char)*c);
        if(*c == '-') *c = '_';
        if(!(islower((unsigned char)*c) || isdigit((unsigned char)*c) || *c == '_')) return 0;
    }
    return 1;
}


void
mb_ini_add(struct ini* ini, int section, const char* key, const char* value, int line)
{
    ini->entries = (struct ini_entry*)mb_xrealloc(ini->entries, (ini->nentries + 1) * sizeof(struct ini_entry));

    struct ini_entry* e = &ini->entries[ini->nentries++];
    e->key = mb_strdup(key);
    e->value = value ? mb_strdup(value) : NULL;
    e->section = section;
    e->line = line;
    e->append = 0;
}


static
int
ini_add_section(struct ini* ini, const char* key, const char* value)
{
    ini->sections = (struct ini_section*)mb_xrealloc(ini->sections, (ini->nsections + 1) * sizeof(struct ini_section));

    struct ini_section* s = &ini->sections[ini->nsections];
    s->key = mb_strdup(key);
    s->value = mb_strdup(value);
    return (int)ini->nsections++;
}


/* "value" | value [# comment]; empty value -> NULL */
static
const char*
ini_parse_value(char* raw, const char* path, int line)
{
    char* v = mb_trim(raw);

    if(*v == '\"')
    {
        char* q = strchr(v + 1, '\"');
        if(!q)
        {
            mb_log(MB_LOG_LEVEL_WARN, "Ini", "%s:%d: missing closing '\"', value ignored", path, line);
            return NULL;
        }
        *q = '\0';
        v++;
    }
    else
    {
        char* comment = strpbrk(v, "#;");
        if(comment) *comment = '\0';
        v = mb_trim(v);
    }

    return *v ? v : NULL;
}


static
int
ini_parse_section(struct ini* ini, char* s, int line)
{
    char* end = strchr(s, ']');
    if(!end)
    {
        mb_log(MB_LOG_LEVEL_WARN, "Ini", "%s:%d: missing ']' in section header", ini->path, line);
        return -2;
    }
    *end = '\0';

    char* name = mb_trim(s + 1);
    char* dot = strchr(name, '.');
    if(!dot) return -1;

    *dot = '\0';
    char* key = mb_trim(name);
    char* value = mb_trim(dot + 1);

    if(!mb_ini_normalize_key(key) || *value == '\0')
    {
        mb_log(MB_LOG_LEVEL_WARN, "Ini", "%s:%d: invalid section header, section skipped", ini->path, line);
        return -2;
    }

    return ini_add_section(ini, key, value);
}


int
mb_ini_load(struct ini* ini, const char* path)
{
    mb_ini_init(ini, path);

    FILE* f = fopen(path, "r");
    if(!f) return 0;

    struct mb_buf buf;
    mb_buf_init(&buf);

    int section = -1;
    int line = 0;
    int rc;

    while((rc = mb_read_line(f, &buf)) > 0)
    {
        line++;

        char* s = mb_trim(buf.data);
        if(*s == '\0' || *s == '#' || *s == ';') continue;

        if(*s == '[')
        {
            section = ini_parse_section(ini, s, line);
            continue;
        }

        if(section == -2) continue;

        char* eq = strchr(s, '=');
        if(!eq)
        {
            mb_log(MB_LOG_LEVEL_WARN, "Ini", "%s:%d: skip line without '=': '%s'", path, line, s);
            continue;
        }

        *eq = '\0';
        char* key = mb_trim(s);

        int append = 0;
        const size_t klen = strlen(key);
        if(klen > 0 && key[klen - 1] == '+')
        {
            key[klen - 1] = '\0';
            key = mb_trim(key);
            append = 1;
        }

        if(!mb_ini_normalize_key(key))
        {
            mb_log(MB_LOG_LEVEL_WARN, "Ini", "%s:%d: invalid key '%s', line skipped", path, line, key);
            continue;
        }

        mb_ini_add(ini, section, key, ini_parse_value(eq + 1, path, line), line);
        ini->entries[ini->nentries - 1].append = append;
    }

    mb_buf_free(&buf);
    fclose(f);

    if(rc < 0)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Ini", "Can't read '%s'", path);
        return 0;
    }

    return 1;
}


void
mb_ini_free(struct ini* ini)
{
    for(size_t i = 0; i < ini->nentries; i++)
    {
        free(ini->entries[i].key);
        free(ini->entries[i].value);
    }

    for(size_t i = 0; i < ini->nsections; i++)
    {
        free(ini->sections[i].key);
        free(ini->sections[i].value);
    }

    free(ini->entries);
    free(ini->sections);
    free(ini->path);
    memset(ini, 0, sizeof(*ini));
}
