#include "generator.h"
#include "logger.h"
#include "template.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define GMK_FILE_NAME "makefile"

#define GMK_PROJECT_BUILD_DIR "$(BUILD_DIR)/$(PROJECT_NAME)"
#define GMK_TARGET "$(BIN_DIR)/$(PROJECT_NAME).$(OUTPUT_EXT)"
#define GMK_ELF "$(BIN_DIR)/$(PROJECT_NAME).elf"


struct gmk_lang
{
    const char* ext;
    const char* cmd;
    const char* flags;
};

static const struct gmk_lang
gmk_langs[] = {
    {"c",   "$(CC)", "$(CFLAGS)"},
    {"cpp", "$(if $(CXX),,$(error microb: 'cxx' is not set for this MCU))$(CXX)", "$(CXXFLAGS)"},
    {"s",   "$(CC)", "$(ASFLAGS)"},
    {"S",   "$(CC)", "$(ASFLAGS)"},
};

#define GMK_LANGS_SIZE (sizeof(gmk_langs) / sizeof(gmk_langs[0]))

/* sources of the project, and extra_sources (found by vpath) built into a separate dir */
static const char* const
gmk_rule_targets[] = {
    "$(PROJECT_BUILD_DIR)/%.o: $(SOURCE_DIR)/%.",
    "$(PROJECT_BUILD_DIR)/extra/%.o: %."
};

#define GMK_RULE_TARGETS_SIZE (sizeof(gmk_rule_targets) / sizeof(gmk_rule_targets[0]))


static const char* const
gmk_rules =
    ".PHONY: all dirs build size flash clean\n"
    "all: dirs build\n"
    "\n"
    "dirs:\n"
    "\t@mkdir -p $(BIN_DIR)\n"
    "\t@mkdir -p $(BUILD_DIR)\n"
    "\t@mkdir -p $(PROJECT_BUILD_DIR)\n"
    "\n"
    "build: $(TARGET)\n"
    "\n"
    "$(TARGET): $(ELF)\n"
    "\t$(OBJCOPY) $(OBJCOPY_FLAGS) $< $@\n"
    "\n"
    "$(ELF): $(OBJS) $(LINKER_SCRIPT)\n"
    "\t@mkdir -p $(@D)\n"
    "\t$(if $(filter %.cpp,$(CXX_SRCS) $(EXTRA_SOURCES)),$(CXX),$(CC)) $(ARCH_FLAGS) -o $@ $(OBJS) $(LINK_FLAGS) $(LDFLAGS)\n"
    "\n";


static
void
gmk_write_rules(struct mb_buf* mk)
{
    for(size_t i = 0; i < GMK_LANGS_SIZE; i++)
        mb_buf_printf(mk, "vpath %%.%s $(sort $(dir $(EXTRA_SOURCES)))\n", gmk_langs[i].ext);
    mb_buf_putc(mk, '\n');

    mb_buf_puts(mk, gmk_rules);

    for(size_t t = 0; t < GMK_RULE_TARGETS_SIZE; t++)
    {
        for(size_t i = 0; i < GMK_LANGS_SIZE; i++)
        {
            mb_buf_printf(mk,
                "%s%s\n"
                "\t@mkdir -p $(@D)\n"
                "\t%s $(ARCH_FLAGS) $(DEFINES) -I$(INCLUDE_DIR) $(INCLUDES) -MMD -MP %s -c $< -o $@\n"
                "\n",
                gmk_rule_targets[t], gmk_langs[i].ext, gmk_langs[i].cmd, gmk_langs[i].flags);
        }
    }

    mb_buf_puts(mk, "-include $(DEPS)\n\n");
}


struct gmk
{
    const struct config* cfg;
    struct config_kv extra[8];
    char* ld_var;
    char* ld_path;
    struct mb_buf ld;
    struct mb_buf mk;
};


static
void
gmk_set_extra(struct gmk* g)
{
    size_t n = 0;
    g->extra[n++] = (struct config_kv){"PROJECT_BUILD_DIR", GMK_PROJECT_BUILD_DIR};
    g->extra[n++] = (struct config_kv){"TARGET", GMK_TARGET};
    g->extra[n++] = (struct config_kv){"ELF", GMK_ELF};
    if(g->ld_var) g->extra[n++] = (struct config_kv){"LINKER_SCRIPT", g->ld_var};
    g->extra[n] = (struct config_kv){NULL, NULL};
}


static
int
gmk_expand_key(const struct gmk* g, const char* key, struct mb_buf* out)
{
    struct mb_buf err;
    mb_buf_init(&err);

    const int rc = mb_config_expand(g->cfg, g->extra, mb_config_get(g->cfg, key), out, &err);
    if(rc != 1)
        mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Can't expand '%s': %s", key, mb_buf_str(&err));

    mb_buf_free(&err);
    return rc == 1;
}


static
char*
gmk_find_template(const struct gmk* g, const char* name)
{
    const struct config* cfg = g->cfg;
    const struct config_var* v = mb_config_find(cfg, "linker_template");
    const struct config_layer* layer = &cfg->layers[v->layer];

    if(!layer->reldir)
        return mb_is_file(name) ? mb_strdup(name) : NULL;

    char* rel = mb_path_join(layer->reldir, name);
    char* path = mb_mcu_db_find(&cfg->db, rel);
    free(rel);
    return path;
}


static
int
gmk_linker_script(struct gmk* g)
{
    const struct config* cfg = g->cfg;
    struct mb_buf value;
    mb_buf_init(&value);
    int ok = 1;

    if(mb_config_get(cfg, "linker_script"))
    {
        ok = gmk_expand_key(g, "linker_script", &value);
        if(ok && !mb_is_file(mb_buf_str(&value)))
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Linker script '%s' not found", mb_buf_str(&value));
            ok = 0;
        }
        if(ok) mb_log(MB_LOG_LEVEL_INFO, "Generator", "Linker script '%s'", mb_buf_str(&value));

        mb_buf_free(&value);
        return ok;
    }

    if(!mb_config_get(cfg, "linker_template"))
    {
        mb_buf_free(&value);
        return 1;
    }

    char* tmpl = NULL;
    if(gmk_expand_key(g, "linker_template", &value))
    {
        tmpl = gmk_find_template(g, mb_buf_str(&value));
        if(!tmpl)
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Linker template '%s' not found", mb_buf_str(&value));
            ok = 0;
        }
    }
    else
    {
        ok = 0;
    }

    if(ok) ok = gmk_expand_key(g, "build_dir", &value);

    if(ok)
    {
        char* name = (char*)mb_xmalloc(strlen(cfg->mcu) + sizeof(".ld"));
        strcpy(name, cfg->mcu);
        strcat(name, ".ld");

        g->ld_path = mb_path_join(mb_buf_str(&value), name);
        g->ld_var = mb_path_join("$(BUILD_DIR)", name);
        free(name);

        gmk_set_extra(g);

        ok = mb_template_render(cfg, g->extra, tmpl, &g->ld);
        if(ok) mb_log(MB_LOG_LEVEL_INFO, "Generator", "Linker script '%s' from '%s'", g->ld_path, tmpl);
    }

    free(tmpl);
    mb_buf_free(&value);
    return ok;
}


/* same basename without dir and extension -> same .o */
static
int
gmk_same_object(const char* a, const char* b)
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


static
int
gmk_check_extra_sources(const struct gmk* g)
{
    const char* sources = mb_config_get(g->cfg, "extra_sources");
    if(!sources) return 1;

    struct mb_buf value;
    struct mb_buf err;
    mb_buf_init(&value);
    mb_buf_init(&err);

    int ok = 1;
    const int rc = mb_config_expand(g->cfg, g->extra, sources, &value, &err);

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
        char** files = NULL;
        size_t nfiles = 0;

        for(char* f = strtok(value.data, " \t"); f; f = strtok(NULL, " \t"))
        {
            if(!mb_is_file(f))
            {
                mb_log(MB_LOG_LEVEL_ERROR, "Generator", "File '%s' from extra_sources not found", f);
                ok = 0;
            }

            /* all extra objects live in one dir: $(PROJECT_BUILD_DIR)/extra/<name>.o */
            for(size_t i = 0; i < nfiles; i++)
            {
                if(!gmk_same_object(files[i], f)) continue;
                mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Files '%s' and '%s' from extra_sources build the same object, rename one",
                    files[i], f);
                ok = 0;
            }

            files = (char**)mb_xrealloc(files, (nfiles + 1) * sizeof(char*));
            files[nfiles++] = f;
        }

        free(files);
    }

    mb_buf_free(&value);
    mb_buf_free(&err);
    return ok;
}


static
void
gmk_put_value(struct mb_buf* mk, const char* value)
{
    for(const char* c = value; *c; c++)
    {
        if(*c == '#') mb_buf_putc(mk, '\\');
        mb_buf_putc(mk, *c);
    }
}


static
int
gmk_write_vars(struct gmk* g)
{
    const struct config* cfg = g->cfg;
    char** names = (char**)mb_xmalloc(cfg->nvars * sizeof(char*));
    const char** keys = (const char**)mb_xmalloc(cfg->nvars * sizeof(char*));
    size_t n = 0;
    int ok = 1;

    for(size_t li = 0; li < cfg->nlayers; li++)
    {
        int first = 1;

        for(size_t i = 0; i < cfg->nvars; i++)
        {
            const struct config_var* v = &cfg->vars[i];
            if(v->layer != (int)li || !v->value) continue;

            char* name = mb_config_var_name(v->key);
            if(!name) continue;

            if(mb_config_is_reserved_var(name))
            {
                if(li + 1 < cfg->nlayers)
                    mb_log(MB_LOG_LEVEL_WARN, "Generator", "Key '%s' (%s): makefile variable %s is reserved by microb, ignored",
                        v->key, cfg->layers[li].name, name);
                free(name);
                continue;
            }

            for(size_t j = 0; j < n; j++)
            {
                if(strcmp(names[j], name) != 0) continue;
                mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Keys '%s' and '%s' are both makefile variable %s", keys[j], v->key, name);
                ok = 0;
            }

            first = 0;

            mb_buf_printf(&g->mk, "%s = ", name);
            gmk_put_value(&g->mk, v->value);
            mb_buf_putc(&g->mk, '\n');

            names[n] = name;
            keys[n] = v->key;
            n++;
        }

        if(!first) mb_buf_putc(&g->mk, '\n');
    }

    for(size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    free(keys);
    return ok;
}


static
int
gmk_write_makefile(struct gmk* g)
{
    const struct config* cfg = g->cfg;
    struct mb_buf* mk = &g->mk;

    if(!gmk_write_vars(g)) return 0;

    mb_buf_puts(mk,
        "PROJECT_BUILD_DIR = " GMK_PROJECT_BUILD_DIR "\n"
        "SRCS = $(wildcard $(SOURCE_DIR)/*.c)\n"
        "CXX_SRCS = $(wildcard $(SOURCE_DIR)/*.cpp)\n"
        "ASM_SRCS = $(sort $(wildcard $(SOURCE_DIR)/*.s $(SOURCE_DIR)/*.S))\n"
        "OBJS = $(patsubst $(SOURCE_DIR)/%,$(PROJECT_BUILD_DIR)/%.o,$(basename $(SRCS) $(CXX_SRCS) $(ASM_SRCS)))"
        " $(addprefix $(PROJECT_BUILD_DIR)/extra/,$(addsuffix .o,$(notdir $(basename $(EXTRA_SOURCES)))))\n"
        "DEPS = $(OBJS:.o=.d)\n"
        "\n"
        "TARGET = " GMK_TARGET "\n"
        "ELF = " GMK_ELF "\n");

    if(g->ld_var) mb_buf_printf(mk, "LINKER_SCRIPT = %s\n", g->ld_var);

    mb_buf_puts(mk, "\n");
    gmk_write_rules(mk);

    if(g->ld_var)
    {
        mb_buf_puts(mk,
            "$(LINKER_SCRIPT):\n"
            "\t@echo \"microb: $@ not found, run microb to generate it\" >&2\n"
            "\t@exit 1\n"
            "\n");
    }

    mb_buf_puts(mk, "size: $(ELF)\n");
    if(mb_config_get(cfg, "size"))
        mb_buf_puts(mk, "\t$(SIZE) $(SIZE_FLAGS) $<\n\n");
    else
        mb_buf_puts(mk, "\t@echo \"microb: 'size' is not set for this MCU\" >&2\n\t@exit 1\n\n");

    mb_buf_puts(mk, "flash: build\n");
    if(mb_config_get(cfg, "upload_cmd"))
    {
        /* keys of the upload method: may be set in microb.ini or on the command line */
        for(size_t i = 0; i < cfg->nflash_reqs; i++)
        {
            const char* key = cfg->flash_reqs[i].key;
            char* var = mb_config_var_name(key);
            if(!var) continue;

            mb_buf_printf(mk,
                "\t$(if $(strip $(%s)),,@echo \"microb: %s is not set - set '%s' in microb.ini or run 'make flash %s=...'\" >&2; exit 1)\n",
                var, var, key, var);
            free(var);
        }
        mb_buf_puts(mk, "\t$(UPLOAD_CMD)\n\n");
    }
    else
        mb_buf_puts(mk, "\t@echo \"microb: upload_cmd is not set, choose 'upload' in microb.ini\" >&2\n\t@exit 1\n\n");

    mb_buf_puts(mk,
        "clean:\n"
        "\trm -rf $(TARGET) $(ELF) $(PROJECT_BUILD_DIR)\n");

    return 1;
}


int
mb_generator__makefile(const struct config* config)
{
    if(!config)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Config not found");
        return 0;
    }

    struct gmk g;
    memset(&g, 0, sizeof(g));
    g.cfg = config;
    mb_buf_init(&g.ld);
    mb_buf_init(&g.mk);
    gmk_set_extra(&g);

    int ok = gmk_linker_script(&g) && gmk_check_extra_sources(&g);

    ok = ok && gmk_write_makefile(&g);

    if(ok && g.ld_path)
    {
        char* dir = mb_dirname(g.ld_path);
        if(!mb_mkdir_p(dir))
        {
            mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Can't create directory '%s'", dir);
            ok = 0;
        }
        free(dir);

        if(ok && !mb_file_same(g.ld_path, mb_buf_str(&g.ld), g.ld.len))
            ok = mb_write_file_atomic(g.ld_path, mb_buf_str(&g.ld), g.ld.len);
    }

    ok = ok && mb_write_file_atomic(GMK_FILE_NAME, mb_buf_str(&g.mk), g.mk.len);

    if(ok)
    {
        mb_log(MB_LOG_LEVEL_INFO, "Generator", "Source path '%s/%s'", config->path, mb_config_get(config, "source_dir"));
        mb_log(MB_LOG_LEVEL_INFO, "Generator", "Binary path '%s/%s'", config->path, mb_config_get(config, "bin_dir"));
        mb_log(MB_LOG_LEVEL_INFO, "Generator", "Build path '%s/%s'", config->path, mb_config_get(config, "build_dir"));
        mb_log(MB_LOG_LEVEL_INFO, "Generator", "Makefile generated successfully");
    }
    else
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Generator", "Failed to generate Makefile, old '%s' is kept", GMK_FILE_NAME);
    }

    free(g.ld_path);
    free(g.ld_var);
    mb_buf_free(&g.ld);
    mb_buf_free(&g.mk);
    return ok;
}
