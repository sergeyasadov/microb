#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "logger.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static const char* const __config_file = "microb.ini";

static const char* const
config_template_keys[] = {
    "project_name",
    "mcu_name",
    "mcu_freq",
    "compiler",
    "objcopy",
    "uploader",
    "programmer",
    "build_dir",
    "bin_dir",
    "include_dir",
    "source_dir",
    "baud",
    "port",
    "cflags",
    "ldflags",
    "cppflags",
    "upload",
    "linker_script"
};

#define CONFIG_TEMPLATE_KEYS_SIZE (sizeof(config_template_keys) / sizeof(config_template_keys[0]))

static const char* const
config_defaults[][2] = {
    {"project_name", "firmware"},
    {"bin_dir", "bin"},
    {"build_dir", "build"},
    {"include_dir", "include"},
    {"source_dir", "src"}
};

#define CONFIG_DEFAULTS_SIZE (sizeof(config_defaults) / sizeof(config_defaults[0]))

static const char* const config_builtin_require = "compiler objcopy output_ext";

static const char* const
config_aliases[][2] = {
    {"mcu_name", "MCU"},
    {"compiler", "CC"},
    {"cppflags", "CXXFLAGS"}
};

#define CONFIG_ALIASES_SIZE (sizeof(config_aliases) / sizeof(config_aliases[0]))

static const char* const
config_hidden_keys[] = {
    "family",
    "require",
    "linker_template"
};

#define CONFIG_HIDDEN_KEYS_SIZE (sizeof(config_hidden_keys) / sizeof(config_hidden_keys[0]))

static const char* const
config_reserved_vars[] = {
    "PROJECT_BUILD_DIR",
    "SRCS",
    "CXX_SRCS",
    "ASM_SRCS",
    "OBJS",
    "DEPS",
    "TARGET",
    "ELF"
};

#define CONFIG_RESERVED_VARS_SIZE (sizeof(config_reserved_vars) / sizeof(config_reserved_vars[0]))

#define CONFIG_EXPAND_MAX_DEPTH 32


static
int
config_in_list(const char* key, const char* const* list, size_t size)
{
    for(size_t i = 0; i < size; i++)
        if(strcmp(key, list[i]) == 0) return 1;
    return 0;
}


static
int
config_is_hidden(const char* key)
{
    return config_in_list(key, config_hidden_keys, CONFIG_HIDDEN_KEYS_SIZE);
}


static
const char*
config_alias(const char* key)
{
    for(size_t i = 0; i < CONFIG_ALIASES_SIZE; i++)
        if(strcmp(key, config_aliases[i][0]) == 0) return config_aliases[i][1];
    return NULL;
}


int
mb_config_is_reserved_var(const char* name)
{
    return config_in_list(name, config_reserved_vars, CONFIG_RESERVED_VARS_SIZE);
}


char*
mb_config_var_name(const char* key)
{
    if(config_is_hidden(key)) return NULL;

    const char* alias = config_alias(key);
    if(alias) return mb_strdup(alias);

    char* name = mb_strdup(key);
    for(char* c = name; *c; c++) *c = (char)toupper((unsigned char)*c);
    return name;
}


static
int
config_key_is_var(const char* key, const char* name)
{
    if(config_is_hidden(key)) return 0;

    const char* alias = config_alias(key);
    if(alias) return strcmp(alias, name) == 0;

    for(; *key && *name; key++, name++)
        if(toupper((unsigned char)*key) != *name) return 0;

    return *key == *name;
}


struct config*
mb_config_init(const char* path)
{
    struct config* cfg = (struct config*)calloc(1, sizeof(struct config));
    if(!cfg) return NULL;

    cfg->path = mb_strdup(path);

    mb_log(MB_LOG_LEVEL_INFO, "Config", "Current path '%s'", cfg->path);

    FILE* existing = fopen(__config_file, "r");
    if(existing)
    {
        fclose(existing);
        return cfg;
    }

    FILE* cfg_file = fopen(__config_file, "w");
    if(!cfg_file)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "Can't create config file '%s'", __config_file);
        return cfg;
    }

    mb_log(MB_LOG_LEVEL_INFO, "Config", "Create new config file '%s'", __config_file);

    for(size_t i = 0; i < CONFIG_TEMPLATE_KEYS_SIZE; i++)
    {
        fprintf(cfg_file, "%s = \"\"\n",  config_template_keys[i]);
        mb_log(MB_LOG_LEVEL_INFO, "Config", "Push key '%s'", config_template_keys[i]);
    }

    fclose(cfg_file);

    return cfg;
}


static
const char*
config_ini_get(const struct ini* ini, const char* key)
{
    const char* value = NULL;
    for(size_t i = 0; i < ini->nentries; i++)
    {
        const struct ini_entry* e = &ini->entries[i];
        if(e->section < 0 && e->value && strcmp(e->key, key) == 0) value = e->value;
    }
    return value;
}


static
int
config_ini_has_key(const struct ini* ini, const char* key)
{
    for(size_t i = 0; i < ini->nentries; i++)
        if(strcmp(ini->entries[i].key, key) == 0) return 1;
    return 0;
}


static
struct config_var*
config_var_get(struct config_var** vars, size_t* n, const char* key, int create)
{
    for(size_t i = 0; i < *n; i++)
        if(strcmp((*vars)[i].key, key) == 0) return &(*vars)[i];

    if(!create) return NULL;

    *vars = (struct config_var*)mb_xrealloc(*vars, (*n + 1) * sizeof(struct config_var));
    struct config_var* v = &(*vars)[(*n)++];
    v->key = key;
    v->value = NULL;
    v->layer = -1;
    v->owned = NULL;
    return v;
}


const struct config_var*
mb_config_find(const struct config* cfg, const char* key)
{
    for(size_t i = 0; i < cfg->nvars; i++)
        if(strcmp(cfg->vars[i].key, key) == 0) return &cfg->vars[i];
    return NULL;
}


const char*
mb_config_get(const struct config* cfg, const char* key)
{
    const struct config_var* v = mb_config_find(cfg, key);
    return v ? v->value : NULL;
}


static
void
config_layer_set(struct config_layer* layer, const char* name, const char* reldir)
{
    layer->name = mb_strdup(name);
    layer->reldir = reldir ? mb_strdup(reldir) : NULL;
}


static
void
config_layer_free(struct config_layer* layer)
{
    free(layer->name);
    free(layer->reldir);
    mb_ini_free(&layer->ini);
    memset(layer, 0, sizeof(*layer));
}


static
size_t
config_load_chain(struct config* cfg, struct config_layer* chain)
{
    char* rel_dir = NULL;
    char* path = mb_mcu_db_find_mcu(&cfg->db, cfg->mcu, &rel_dir);

    if(!path)
    {
        char* roots = mb_mcu_db_roots_str(&cfg->db);
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "MCU '%s' not found in MCU database (%s)", cfg->mcu, roots);
        free(roots);
        return 0;
    }

    mb_log(MB_LOG_LEVEL_INFO, "Config", "MCU '%s': '%s'", cfg->mcu, path);

    size_t n = 0;
    config_layer_set(&chain[n], path, rel_dir);
    free(rel_dir);

    int ok = mb_ini_load(&chain[n].ini, path);
    free(path);
    if(!ok)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "Can't read '%s'", chain[n].name);
        config_layer_free(&chain[n]);
        return 0;
    }
    n++;

    const char* parent = config_ini_get(&chain[0].ini, "family");

    while(parent)
    {
        char* family = mb_strdup(parent);
        mb_str_lower(family);

        const char* child = chain[n - 1].name;

        if(!mb_mcu_db_valid_name(family))
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Config", "Invalid family name '%s' in '%s'", family, child);
            ok = 0;
        }

        if(ok && n > MB_CONFIG_MAX_FAMILY_DEPTH)
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Config", "Family chain of '%s' is too deep (max %d)", cfg->mcu, MB_CONFIG_MAX_FAMILY_DEPTH);
            ok = 0;
        }

        char* rel = NULL;
        if(ok)
        {
            rel = (char*)mb_xmalloc(strlen("family/") + strlen(family) + sizeof(".ini"));
            sprintf(rel, "family/%s.ini", family);
        }

        char* fpath = ok ? mb_mcu_db_find(&cfg->db, rel) : NULL;
        if(ok && !fpath)
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Config", "Family '%s' (from '%s') not found: no '%s' in MCU database", family, child, rel);
            ok = 0;
        }

        for(size_t i = 1; ok && i < n; i++)
        {
            if(strcmp(chain[i].name, fpath) == 0)
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Config", "Family loop: '%s' includes '%s' again", child, fpath);
                ok = 0;
            }
        }

        if(ok)
        {
            config_layer_set(&chain[n], fpath, "family");
            if(!mb_ini_load(&chain[n].ini, fpath))
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Config", "Can't read '%s'", fpath);
                config_layer_free(&chain[n]);
                ok = 0;
            }
            else
            {
                mb_log(MB_LOG_LEVEL_INFO, "Config", "Family '%s': '%s'", family, fpath);
                n++;
            }
        }

        free(rel);
        free(fpath);
        free(family);

        if(!ok)
        {
            for(size_t i = 0; i < n; i++) config_layer_free(&chain[i]);
            return 0;
        }

        parent = config_ini_get(&chain[n - 1].ini, "family");
    }

    return n;
}


static
void
config_add_req(struct config_req** reqs, size_t* n, const char* list, const char* by)
{
    char* copy = mb_strdup(list);

    for(char* tok = strtok(copy, " \t"); tok; tok = strtok(NULL, " \t"))
    {
        if(!mb_ini_normalize_key(tok))
        {
            mb_log(MB_LOG_LEVEL_WARN, "Config", "Invalid key '%s' in require of %s", tok, by);
            continue;
        }

        int dup = 0;
        for(size_t i = 0; i < *n && !dup; i++) dup = strcmp((*reqs)[i].key, tok) == 0;
        if(dup) continue;

        *reqs = (struct config_req*)mb_xrealloc(*reqs, (*n + 1) * sizeof(struct config_req));
        (*reqs)[*n].key = mb_strdup(tok);
        (*reqs)[*n].by = mb_strdup(by);
        (*n)++;
    }

    free(copy);
}


static
char*
config_entry_origin(const struct config_layer* layer, int section)
{
    struct mb_buf b;
    mb_buf_init(&b);

    if(section < 0)
    {
        mb_buf_printf(&b, "'%s'", layer->name);
    }
    else
    {
        const struct ini_section* s = &layer->ini.sections[section];
        mb_buf_printf(&b, "'%s' [%s.%s]", layer->name, s->key, s->value);
    }

    char* origin = mb_strdup(mb_buf_str(&b));
    mb_buf_free(&b);
    return origin;
}


static
int
config_section_active(const struct ini_section* s, struct config_var* base, size_t nbase)
{
    struct config_var* v = config_var_get(&base, &nbase, s->key, 0);
    return v && v->value && mb_strcaseeq(v->value, s->value);
}


static
void
config_apply(
    struct config* cfg,
    size_t li,
    const struct ini_entry* e,
    struct config_req** reqs,
    size_t* nreqs)
{
    const struct config_layer* layer = &cfg->layers[li];
    const int user = li == cfg->nlayers - 1;

    if(strcmp(e->key, "require") == 0)
    {
        if(e->value)
        {
            char* by = config_entry_origin(layer, e->section);

            /* upload settings are needed only by 'make flash', building must work without them */
            const int flash = e->section >= 0 && strcmp(layer->ini.sections[e->section].key, "upload") == 0;
            if(flash)
                config_add_req(&cfg->flash_reqs, &cfg->nflash_reqs, e->value, by);
            else
                config_add_req(reqs, nreqs, e->value, by);

            free(by);
        }
        return;
    }

    if(user && strcmp(e->key, "family") == 0)
    {
        mb_log(MB_LOG_LEVEL_WARN, "Config", "%s:%d: 'family' is set by the MCU file, ignored", layer->name, e->line);
        return;
    }

    struct config_var* v = config_var_get(&cfg->vars, &cfg->nvars, e->key, 1);
    if(!e->value) return;

    if(e->append && v->value)
    {
        struct mb_buf b;
        mb_buf_init(&b);
        mb_buf_printf(&b, "%s %s", v->value, e->value);

        free(v->owned);
        v->owned = mb_strdup(mb_buf_str(&b));
        v->value = v->owned;
        mb_buf_free(&b);
    }
    else
    {
        free(v->owned);
        v->owned = NULL;
        v->value = e->value;
    }

    v->layer = (int)li;
}


static
int
config_merge(struct config* cfg)
{
    struct config_var* base = NULL;
    size_t nbase = 0;

    for(size_t li = 0; li < cfg->nlayers; li++)
    {
        const struct ini* ini = &cfg->layers[li].ini;
        for(size_t i = 0; i < ini->nentries; i++)
        {
            const struct ini_entry* e = &ini->entries[i];
            if(e->section >= 0 || !e->value || e->append || strcmp(e->key, "require") == 0) continue;
            config_var_get(&base, &nbase, e->key, 1)->value = e->value;
        }
    }

    struct config_var* mcu = config_var_get(&base, &nbase, "mcu_name", 0);
    if(mcu) mcu->value = cfg->mcu;

    struct config_req* reqs = NULL;
    size_t nreqs = 0;

    config_add_req(&reqs, &nreqs, config_builtin_require, "microb");

    for(size_t li = 0; li < cfg->nlayers; li++)
    {
        const struct ini* ini = &cfg->layers[li].ini;

        for(size_t i = 0; i < ini->nentries; i++)
            if(ini->entries[i].section < 0)
                config_apply(cfg, li, &ini->entries[i], &reqs, &nreqs);

        for(size_t i = 0; i < ini->nentries; i++)
        {
            const struct ini_entry* e = &ini->entries[i];
            if(e->section >= 0 && config_section_active(&ini->sections[e->section], base, nbase))
                config_apply(cfg, li, e, &reqs, &nreqs);
        }
    }

    free(base);

    struct config_var* mcu_var = config_var_get(&cfg->vars, &cfg->nvars, "mcu_name", 0);
    if(mcu_var) mcu_var->value = cfg->mcu;

    int ok = 1;

    for(size_t i = 0; i < nreqs; i++)
    {
        if(!mb_config_get(cfg, reqs[i].key))
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Config", "Key '%s' is required by %s, set it in %s", reqs[i].key, reqs[i].by, __config_file);
            ok = 0;
        }
        free(reqs[i].key);
        free(reqs[i].by);
    }
    free(reqs);

    for(size_t i = 0; i < cfg->nflash_reqs; i++)
    {
        if(!mb_config_get(cfg, cfg->flash_reqs[i].key))
            mb_log(MB_LOG_LEVEL_WARN, "Config", "Key '%s' is required by %s for 'make flash', set it in %s",
                cfg->flash_reqs[i].key, cfg->flash_reqs[i].by, __config_file);
    }

    return ok;
}


static
int
config_check_upload(const struct config* cfg)
{
    const char* upload = mb_config_get(cfg, "upload");
    if(!upload) return 1;

    struct mb_buf avail;
    mb_buf_init(&avail);

    int found = 0;

    for(size_t li = 0; li < cfg->nlayers && !found; li++)
    {
        const struct ini* ini = &cfg->layers[li].ini;
        for(size_t i = 0; i < ini->nsections && !found; i++)
        {
            const struct ini_section* s = &ini->sections[i];
            if(strcmp(s->key, "upload") != 0) continue;

            if(mb_strcaseeq(s->value, upload))
            {
                found = 1;
                break;
            }

            int dup = 0;
            for(size_t lj = 0; lj <= li && !dup; lj++)
            {
                const struct ini* prev = &cfg->layers[lj].ini;
                const size_t end = lj == li ? i : prev->nsections;
                for(size_t j = 0; j < end && !dup; j++)
                    dup = strcmp(prev->sections[j].key, "upload") == 0 && mb_strcaseeq(prev->sections[j].value, s->value);
            }

            if(!dup) mb_buf_printf(&avail, "%s%s", avail.len ? ", " : "", s->value);
        }
    }

    if(!found)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "Upload method '%s' is not supported for '%s'. Available: %s",
            upload, cfg->mcu, avail.len ? mb_buf_str(&avail) : "none");
    }

    mb_buf_free(&avail);
    return found;
}


static
void
config_check_unknown(const struct config* cfg)
{
    const struct ini* user = &cfg->layers[cfg->nlayers - 1].ini;

    for(size_t i = 0; i < user->nentries; i++)
    {
        const char* key = user->entries[i].key;
        const int line = user->entries[i].line;

        int reported = 0;
        for(size_t j = 0; j < i && !reported; j++)
            reported = strcmp(user->entries[j].key, key) == 0;
        if(reported) continue;

        char* var = mb_config_var_name(key);

        int known = !var || config_in_list(key, config_template_keys, CONFIG_TEMPLATE_KEYS_SIZE) ||
            strcmp(key, "require") == 0;

        for(size_t li = 0; li + 1 < cfg->nlayers && !known; li++)
            known = config_ini_has_key(&cfg->layers[li].ini, key);

        if(var && mb_config_is_reserved_var(var))
            mb_log(MB_LOG_LEVEL_WARN, "Config", "Key '%s' (line %d): makefile variable %s is reserved by microb, ignored", key, line, var);
        else if(!known)
            mb_log(MB_LOG_LEVEL_WARN, "Config", "Unknown key '%s' (line %d), written to makefile as %s", key, line, var);

        free(var);
    }
}


int
mb_config_parse(struct config** pcfg)
{
    if(!pcfg || !*pcfg) return 0;

    struct config* cfg = *pcfg;

    struct ini user;
    if(!mb_ini_load(&user, __config_file))
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "Can't open config file '%s'", __config_file);
        mb_ini_free(&user);
        return 0;
    }

    for(size_t i = 0; i < user.nentries; i++)
    {
        if(user.entries[i].value)
            mb_log(MB_LOG_LEVEL_INFO, "Config", "%s = '%s'", user.entries[i].key, user.entries[i].value);
    }

    const char* mcu_name = config_ini_get(&user, "mcu_name");
    if(!mcu_name)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "mcu_name is not set in '%s', set it and run microb again", __config_file);
        mb_ini_free(&user);
        return 0;
    }

    cfg->mcu = mb_strdup(mcu_name);
    mb_str_lower(cfg->mcu);

    if(!mb_mcu_db_valid_name(cfg->mcu))
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Config", "Invalid mcu_name '%s'", mcu_name);
        mb_ini_free(&user);
        return 0;
    }

    mb_mcu_db_init(&cfg->db);

    struct config_layer chain[MB_CONFIG_MAX_FAMILY_DEPTH + 1];
    memset(chain, 0, sizeof(chain));

    const size_t nchain = config_load_chain(cfg, chain);
    if(nchain == 0)
    {
        mb_ini_free(&user);
        return 0;
    }

    struct config_layer* layer = &cfg->layers[cfg->nlayers++];
    config_layer_set(layer, "microb defaults", NULL);
    mb_ini_init(&layer->ini, layer->name);
    for(size_t i = 0; i < CONFIG_DEFAULTS_SIZE; i++)
        mb_ini_add(&layer->ini, -1, config_defaults[i][0], config_defaults[i][1], 0);

    for(size_t i = nchain; i-- > 0;)
        cfg->layers[cfg->nlayers++] = chain[i];

    layer = &cfg->layers[cfg->nlayers++];
    config_layer_set(layer, __config_file, NULL);
    layer->ini = user;

    config_check_unknown(cfg);

    return config_merge(cfg) && config_check_upload(cfg);
}


void
mb_config_free(struct config** cfg)
{
    if(cfg && *cfg)
    {
        for(size_t i = 0; i < (*cfg)->nlayers; i++)
            config_layer_free(&(*cfg)->layers[i]);

        for(size_t i = 0; i < (*cfg)->nflash_reqs; i++)
        {
            free((*cfg)->flash_reqs[i].key);
            free((*cfg)->flash_reqs[i].by);
        }
        free((*cfg)->flash_reqs);

        for(size_t i = 0; i < (*cfg)->nvars; i++)
            free((*cfg)->vars[i].owned);

        mb_mcu_db_free(&(*cfg)->db);
        free((*cfg)->vars);
        free((*cfg)->mcu);
        free((*cfg)->path);
        free(*cfg);
        *cfg = NULL;
    }
}


static
int
config_valid_var_name(const char* s, size_t n)
{
    if(n == 0) return 0;
    for(size_t i = 0; i < n; i++)
        if(!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    return 1;
}


static
const char*
config_lookup(const struct config* cfg, const struct config_kv* extra, const char* name)
{
    for(size_t i = 0; i < cfg->nvars; i++)
    {
        const struct config_var* v = &cfg->vars[i];
        if(!config_key_is_var(v->key, name)) continue;
        if(v->value) return v->value;

        const char* env = getenv(name);
        return env ? env : "";
    }

    for(; extra && extra->name; extra++)
        if(strcmp(extra->name, name) == 0) return extra->value;

    if(strcmp(name, "CURDIR") == 0) return cfg->path;

    return getenv(name);
}


static
int
config_expand_rec(
    const struct config* cfg,
    const struct config_kv* extra,
    const char* s,
    struct mb_buf* out,
    struct mb_buf* err,
    int depth)
{
    if(depth > CONFIG_EXPAND_MAX_DEPTH)
    {
        mb_buf_printf(err, "recursive variable loop in '%s'", s);
        return 0;
    }

    for(const char* p = s; *p;)
    {
        if(*p != '$')
        {
            mb_buf_putc(out, *p++);
            continue;
        }

        const char c = p[1];

        if(c == '$')
        {
            mb_buf_putc(out, '$');
            p += 2;
            continue;
        }

        if(c == '\0')
        {
            mb_buf_putc(out, '$');
            p++;
            continue;
        }

        if(c != '(' && c != '{')
        {
            mb_buf_printf(err, "unsupported make construct '$%c'", c);
            return -1;
        }

        const char close = c == '(' ? ')' : '}';
        const char* start = p + 2;
        const char* q = start;
        int level = 1;

        for(; *q; q++)
        {
            if(*q == c) level++;
            else if(*q == close && --level == 0) break;
        }

        if(*q == '\0')
        {
            mb_buf_printf(err, "unterminated '$%c' in '%s'", c, s);
            return 0;
        }

        const size_t n = (size_t)(q - start);
        if(!config_valid_var_name(start, n))
        {
            mb_buf_printf(err, "unsupported make construct '%.*s'", (int)(q - p + 1), p);
            return -1;
        }

        char* name = mb_strndup(start, n);
        const char* value = config_lookup(cfg, extra, name);

        if(!value)
        {
            mb_buf_printf(err, "variable '%s' is not defined", name);
            free(name);
            return 0;
        }
        free(name);

        const int rc = config_expand_rec(cfg, extra, value, out, err, depth + 1);
        if(rc != 1) return rc;

        p = q + 1;
    }

    return 1;
}


int
mb_config_expand(
    const struct config* cfg,
    const struct config_kv* extra,
    const char* s,
    struct mb_buf* out,
    struct mb_buf* err)
{
    mb_buf_reset(out);
    mb_buf_putn(out, "", 0);
    mb_buf_reset(err);
    return config_expand_rec(cfg, extra, s, out, err, 0);
}
