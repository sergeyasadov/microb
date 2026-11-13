#include "template.h"
#include "ini.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define TEMPLATE_MAX_DEPTH 16


static
char*
template_if_key(char* line)
{
    const size_t len = strlen(line);
    if(len < 6 || strncmp(line, "@if", 3) != 0 || line[3] != ' ' || line[len - 1] != '@') return NULL;

    line[len - 1] = '\0';
    return mb_trim(line + 4);
}


static
int
template_line(
    const struct config* cfg,
    const struct config_kv* extra,
    const char* path,
    int lineno,
    const char* line,
    struct mb_buf* out)
{
    struct mb_buf value;
    struct mb_buf err;
    mb_buf_init(&value);
    mb_buf_init(&err);

    int ok = 1;

    for(const char* p = line; *p && ok;)
    {
        if(*p != '@')
        {
            mb_buf_putc(out, *p++);
            continue;
        }

        if(p[1] == '@')
        {
            mb_buf_putc(out, '@');
            p += 2;
            continue;
        }

        const char* end = strchr(p + 1, '@');
        char* key = end ? mb_strndup(p + 1, (size_t)(end - p - 1)) : NULL;

        if(!key || !mb_ini_normalize_key(key))
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: stray '@' (use '@@' for '@')", path, lineno);
            ok = 0;
        }
        else
        {
            const char* v = mb_config_get(cfg, key);
            if(!v)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: key '%s' is not set", path, lineno, key);
                ok = 0;
            }
            else if(mb_config_expand(cfg, extra, v, &value, &err) != 1)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: can't expand '%s': %s", path, lineno, key, mb_buf_str(&err));
                ok = 0;
            }
            else
            {
                mb_buf_puts(out, mb_buf_str(&value));
                p = end + 1;
            }
        }

        free(key);
    }

    mb_buf_putc(out, '\n');

    mb_buf_free(&value);
    mb_buf_free(&err);
    return ok;
}


int
mb_template_render(
    const struct config* cfg,
    const struct config_kv* extra,
    const char* path,
    struct mb_buf* out)
{
    FILE* f = fopen(path, "r");
    if(!f)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Template", "Can't open '%s'", path);
        return 0;
    }

    struct mb_buf line;
    mb_buf_init(&line);

    int cond[TEMPLATE_MAX_DEPTH];
    int seen_else[TEMPLATE_MAX_DEPTH];
    int depth = 0;
    int active = 1;

    int lineno = 0;
    int ok = 1;
    int rc = 0;

    while(ok && (rc = mb_read_line(f, &line)) > 0)
    {
        lineno++;

        char* raw = line.data;
        char* t = mb_trim(raw);
        char* key = template_if_key(t);

        if(key)
        {
            if(depth == TEMPLATE_MAX_DEPTH)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: @if@ nested too deep", path, lineno);
                ok = 0;
                break;
            }
            if(!mb_ini_normalize_key(key))
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: invalid key in @if@", path, lineno);
                ok = 0;
                break;
            }
            cond[depth] = mb_config_get(cfg, key) != NULL;
            seen_else[depth] = 0;
            depth++;
        }
        else if(strcmp(t, "@else@") == 0)
        {
            if(depth == 0 || seen_else[depth - 1])
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: unexpected @else@", path, lineno);
                ok = 0;
                break;
            }
            cond[depth - 1] = !cond[depth - 1];
            seen_else[depth - 1] = 1;
        }
        else if(strcmp(t, "@endif@") == 0)
        {
            if(depth == 0)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s:%d: unexpected @endif@", path, lineno);
                ok = 0;
                break;
            }
            depth--;
        }
        else
        {
            if(active) ok = template_line(cfg, extra, path, lineno, raw, out);
            continue;
        }

        active = 1;
        for(int i = 0; i < depth; i++) active = active && cond[i];
    }

    if(ok && rc < 0)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Template", "Can't read '%s'", path);
        ok = 0;
    }

    if(ok && depth != 0)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Template", "%s: missing @endif@", path);
        ok = 0;
    }

    mb_buf_free(&line);
    fclose(f);
    return ok;
}
