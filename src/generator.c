#include "generator.h"
#include "logger.h"
#include "template.h"

#include <stdlib.h>
#include <string.h>


static const struct mb_generator* const
generators[] = {
    &mb_generator_makefile,
    &mb_generator_cmake,
    &mb_generator_xmake,
    NULL
};


const struct mb_generator* const*
mb_generator_all(void)
{
    return generators;
}


const struct mb_generator*
mb_generator_find(const char* name)
{
    for(size_t i = 0; generators[i]; i++)
        if(mb_strcaseeq(generators[i]->name, name)) return generators[i];
    return NULL;
}


void
mb_gen_vars_init(struct mb_gen_vars* v)
{
    memset(v, 0, sizeof(*v));
}


void
mb_gen_vars_set(struct mb_gen_vars* v, const char* name, const char* value, int literal)
{
    for(size_t i = 0; i < v->n; i++)
    {
        if(strcmp(v->kv[i].name, name) != 0) continue;
        free((char*)v->kv[i].value);
        v->kv[i].value = mb_strdup(value);
        v->kv[i].literal = literal;
        return;
    }

    if(v->n == MB_GEN_MAX_VARS)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Too many generator variables, '%s' dropped", name);
        return;
    }

    v->kv[v->n].name = mb_strdup(name);
    v->kv[v->n].value = mb_strdup(value);
    v->kv[v->n].literal = literal;
    v->n++;
    v->kv[v->n] = (struct config_kv){NULL, NULL, 0};
}


void
mb_gen_vars_free(struct mb_gen_vars* v)
{
    for(size_t i = 0; i < v->n; i++)
    {
        free((char*)v->kv[i].name);
        free((char*)v->kv[i].value);
    }
    mb_gen_vars_init(v);
}


int
mb_gen_expand(const struct config* cfg, const struct mb_gen_vars* v, const char* key, struct mb_buf* out)
{
    struct mb_buf err;
    mb_buf_init(&err);

    const int rc = mb_config_expand(cfg, v ? v->kv : NULL, mb_config_get(cfg, key), out, &err);
    if(rc != 1)
        mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Can't expand '%s': %s", key, mb_buf_str(&err));

    mb_buf_free(&err);
    return rc == 1;
}


char**
mb_gen_args(const struct config* cfg, const struct mb_gen_vars* v, const char* key, size_t* n, int* ok)
{
    struct mb_buf value;
    mb_buf_init(&value);

    char** args = NULL;
    *n = 0;

    if(mb_gen_expand(cfg, v, key, &value))
        args = mb_split_args(mb_buf_str(&value), n);
    else
        *ok = 0;

    mb_buf_free(&value);
    return args;
}


static
void
ld_vars(struct mb_gen_vars* v, const char* script)
{
    mb_gen_vars_init(v);
    mb_gen_vars_set(v, "PROJECT_BUILD_DIR", "$(BUILD_DIR)/$(PROJECT_NAME)", 0);
    mb_gen_vars_set(v, "TARGET", "$(BIN_DIR)/$(PROJECT_NAME).$(OUTPUT_EXT)", 0);
    mb_gen_vars_set(v, "ELF", "$(BIN_DIR)/$(PROJECT_NAME).elf", 0);
    if(script) mb_gen_vars_set(v, "LINKER_SCRIPT", script, 1);
}


static
char*
ld_find_template(const struct config* cfg, const char* name)
{
    const struct config_var* var = mb_config_find(cfg, "linker_template");
    const struct config_layer* layer = &cfg->layers[var->layer];

    if(!layer->reldir)
        return mb_is_file(name) ? mb_strdup(name) : NULL;

    char* rel = mb_path_join(layer->reldir, name);
    char* path = mb_mcu_db_find(&cfg->db, rel);
    free(rel);
    return path;
}


int
mb_ldscript_prepare(const struct config* cfg, struct mb_ldscript* ld)
{
    memset(ld, 0, sizeof(*ld));
    mb_buf_init(&ld->text);

    struct mb_gen_vars v;
    ld_vars(&v, NULL);

    struct mb_buf value;
    mb_buf_init(&value);
    int ok = 1;

    if(mb_config_get(cfg, "linker_script"))
    {
        ok = mb_gen_expand(cfg, &v, "linker_script", &value);
        if(ok && !mb_is_file(mb_buf_str(&value)))
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Linker script '%s' not found", mb_buf_str(&value));
            ok = 0;
        }
        if(ok)
        {
            ld->path = mb_strdup(mb_buf_str(&value));
            mb_log(MB_LOG_LEVEL_INFO, "Generator", "Linker script '%s'", ld->path);
        }
    }
    else if(mb_config_get(cfg, "linker_template"))
    {
        char* tmpl = NULL;

        ok = mb_gen_expand(cfg, &v, "linker_template", &value);
        if(ok)
        {
            tmpl = ld_find_template(cfg, mb_buf_str(&value));
            if(!tmpl)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Linker template '%s' not found", mb_buf_str(&value));
                ok = 0;
            }
        }

        ok = ok && mb_gen_expand(cfg, &v, "build_dir", &value);

        if(ok)
        {
            char* name = (char*)mb_xmalloc(strlen(cfg->mcu) + sizeof(".ld"));
            strcpy(name, cfg->mcu);
            strcat(name, ".ld");

            ld->path = mb_path_join(mb_buf_str(&value), name);
            ld->generated = 1;
            free(name);

            mb_gen_vars_free(&v);
            ld_vars(&v, ld->path);

            ok = mb_template_render(cfg, v.kv, tmpl, &ld->text);
            if(ok) mb_log(MB_LOG_LEVEL_INFO, "Generator", "Linker script '%s' from '%s'", ld->path, tmpl);
        }

        free(tmpl);
    }

    mb_buf_free(&value);
    mb_gen_vars_free(&v);

    if(!ok) mb_ldscript_free(ld);
    return ok;
}


int
mb_ldscript_write(const struct mb_ldscript* ld)
{
    return !ld->generated || mb_gen_write(ld->path, &ld->text);
}


void
mb_ldscript_free(struct mb_ldscript* ld)
{
    free(ld->path);
    mb_buf_free(&ld->text);
    memset(ld, 0, sizeof(*ld));
}


static
int
gen_same_object(const char* a, const char* b)
{
    const char* sa = strrchr(a, '/');
    const char* sb = strrchr(b, '/');
    a = sa ? sa + 1 : a;
    b = sb ? sb + 1 : b;

    const char* da = strrchr(a, '.');
    const char* db = strrchr(b, '.');
    const size_t la = da ? (size_t)(da - a) : strlen(a);
    const size_t lb = db ? (size_t)(db - b) : strlen(b);

    return la == lb && strncmp(a, b, la) == 0;
}


int
mb_gen_check_extra_sources(const struct config* cfg, const struct mb_gen_vars* v)
{
    const char* sources = mb_config_get(cfg, "extra_sources");
    if(!sources) return 1;

    struct mb_buf value;
    struct mb_buf err;
    mb_buf_init(&value);
    mb_buf_init(&err);

    int ok = 1;
    const int rc = mb_config_expand(cfg, v ? v->kv : NULL, sources, &value, &err);

    if(rc < 0)
    {
        mb_log(MB_LOG_LEVEL_WARN, "Generator", "extra_sources not checked: %s", mb_buf_str(&err));
    }
    else if(rc == 0)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Can't expand 'extra_sources': %s", mb_buf_str(&err));
        ok = 0;
    }
    else
    {
        size_t n = 0;
        char** files = mb_split_args(mb_buf_str(&value), &n);

        for(size_t i = 0; i < n; i++)
        {
            if(!mb_is_file(files[i]))
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Generator", "File '%s' from extra_sources not found", files[i]);
                ok = 0;
            }

            for(size_t j = 0; j < i; j++)
            {
                if(!gen_same_object(files[j], files[i])) continue;
                mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Files '%s' and '%s' from extra_sources build the same object, rename one",
                    files[j], files[i]);
                ok = 0;
            }
        }

        mb_strv_free(files, n);
    }

    mb_buf_free(&value);
    mb_buf_free(&err);
    return ok;
}


int
mb_gen_write(const char* path, const struct mb_buf* text)
{
    if(mb_file_same(path, mb_buf_str(text), text->len))
    {
        mb_log(MB_LOG_LEVEL_INFO, "Generator", "'%s' is up to date", path);
        return 1;
    }

    char* dir = mb_dirname(path);
    const int dir_ok = strcmp(dir, ".") == 0 || mb_mkdir_p(dir);
    if(!dir_ok) mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Can't create directory '%s'", dir);
    free(dir);

    if(!dir_ok || !mb_write_file_atomic(path, mb_buf_str(text), text->len)) return 0;

    mb_log(MB_LOG_LEVEL_INFO, "Generator", "Wrote '%s'", path);
    return 1;
}


enum mb_flag_kind
mb_gen_flag(char* const* args, size_t n, size_t* i, const char** value)
{
    static const struct { const char* prefix; enum mb_flag_kind kind; } kinds[] = {
        {"-D", MB_FLAG_DEFINE},
        {"-I", MB_FLAG_INCLUDE},
        {"-l", MB_FLAG_LIB},
    };

    const char* a = args[*i];

    for(size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
    {
        if(strncmp(a, kinds[k].prefix, 2) != 0) continue;

        if(a[2] != '\0')
        {
            *value = a + 2;
            (*i)++;
            return kinds[k].kind;
        }
        if(*i + 1 < n)
        {
            *value = args[*i + 1];
            *i += 2;
            return kinds[k].kind;
        }
    }

    *value = a;
    (*i)++;
    return MB_FLAG_OPTION;
}
