#include "generator.h"
#include "logger.h"

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

static const char* const
gmk_rule_targets[] = {
    "$(PROJECT_BUILD_DIR)/%.o: $(SOURCE_DIR)/%.",
    "$(PROJECT_BUILD_DIR)/extra/%.o: %."
};

#define GMK_RULE_TARGETS_SIZE (sizeof(gmk_rule_targets) / sizeof(gmk_rule_targets[0]))


static const char* const
gmk_rules =
    ".PHONY: all dirs build size flash clean compile_commands.json\n"
    "all: dirs build compile_commands.json\n"
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
gmk_write_vars(const struct config* cfg, struct mb_buf* mk)
{
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

            mb_buf_printf(mk, "%s = ", name);
            gmk_put_value(mk, v->value);
            mb_buf_putc(mk, '\n');

            names[n] = name;
            keys[n] = v->key;
            n++;
        }

        if(!first) mb_buf_putc(mk, '\n');
    }

    for(size_t i = 0; i < n; i++) free(names[i]);
    free(names);
    free(keys);
    return ok;
}


static
int
gmk_write_makefile(const struct config* cfg, const char* ld_var, struct mb_buf* mk)
{
    if(!gmk_write_vars(cfg, mk)) return 0;

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

    if(ld_var) mb_buf_printf(mk, "LINKER_SCRIPT = %s\n", ld_var);

    mb_buf_puts(mk,
        "\n"
        "MICROB_JSON = $(subst \",\\\",$(subst \\,\\\\,$(1)))\n"
        "MICROB_SQ = $(subst ','\\'',$(1))\n"
        "MICROB_CDB = '$(call MICROB_SQ,$(call MICROB_JSON,$(1)))'\n"
        "MICROB_CDB_C = $(CC) $(ARCH_FLAGS) $(DEFINES) -I$(INCLUDE_DIR) $(INCLUDES) $(CFLAGS)\n"
        "MICROB_CDB_CXX = $(CXX) $(ARCH_FLAGS) $(DEFINES) -I$(INCLUDE_DIR) $(INCLUDES) $(CXXFLAGS)\n");

    mb_buf_puts(mk, "\n");
    gmk_write_rules(mk);

    if(ld_var)
    {
        mb_buf_puts(mk,
            "$(LINKER_SCRIPT):\n"
            "\t@echo \"microb: $@ not found, run microb to generate it\" >&2\n"
            "\t@exit 1\n"
            "\n");
    }

    mb_buf_puts(mk,
        "compile_commands.json:\n"
        "\t@{ printf '['; sep=''; \\\n"
        "\tfor f in $(SRCS) $(filter %.c,$(EXTRA_SOURCES)); do \\\n"
        "\tprintf '%s\\n  {\"directory\": \"%s\", \"file\": \"%s\", \"command\": \"%s -c %s\"}' "
        "\"$$sep\" $(call MICROB_CDB,$(CURDIR)) \"$$f\" $(call MICROB_CDB,$(MICROB_CDB_C)) \"$$f\"; sep=','; \\\n"
        "\tdone; \\\n"
        "\tfor f in $(CXX_SRCS) $(filter %.cpp,$(EXTRA_SOURCES)); do \\\n"
        "\tprintf '%s\\n  {\"directory\": \"%s\", \"file\": \"%s\", \"command\": \"%s -c %s\"}' "
        "\"$$sep\" $(call MICROB_CDB,$(CURDIR)) \"$$f\" $(call MICROB_CDB,$(MICROB_CDB_CXX)) \"$$f\"; sep=','; \\\n"
        "\tdone; \\\n"
        "\tprintf '\\n]\\n'; } > $@.tmp\n"
        "\t@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; fi\n"
        "\n");

    mb_buf_puts(mk, "size: $(ELF)\n");
    if(mb_config_get(cfg, "size"))
        mb_buf_puts(mk, "\t$(SIZE) $(SIZE_FLAGS) $<\n\n");
    else
        mb_buf_puts(mk, "\t@echo \"microb: 'size' is not set for this MCU\" >&2\n\t@exit 1\n\n");

    mb_buf_puts(mk, "flash: build\n");
    if(mb_config_get(cfg, "upload_cmd"))
    {
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
    {
        mb_buf_puts(mk, "\t@echo \"microb: upload_cmd is not set, choose 'upload' in microb.ini\" >&2\n\t@exit 1\n\n");
    }

    mb_buf_puts(mk,
        "clean:\n"
        "\trm -rf $(TARGET) $(ELF) $(PROJECT_BUILD_DIR)\n");

    return 1;
}


static
int
gmk_generate(const struct config* cfg)
{
    struct mb_gen_vars v;
    mb_gen_vars_init(&v);
    mb_gen_vars_set(&v, "PROJECT_BUILD_DIR", GMK_PROJECT_BUILD_DIR, 0);
    mb_gen_vars_set(&v, "TARGET", GMK_TARGET, 0);
    mb_gen_vars_set(&v, "ELF", GMK_ELF, 0);

    struct mb_buf mk;
    mb_buf_init(&mk);

    struct mb_ldscript ld;
    int ok = mb_ldscript_prepare(cfg, &ld);

    char* ld_var = NULL;
    if(ok && ld.generated)
    {
        char* name = (char*)mb_xmalloc(strlen(cfg->mcu) + sizeof(".ld"));
        strcpy(name, cfg->mcu);
        strcat(name, ".ld");
        ld_var = mb_path_join("$(BUILD_DIR)", name);
        free(name);
    }

    ok = ok && mb_gen_check_extra_sources(cfg, &v);
    ok = ok && gmk_write_makefile(cfg, ld_var, &mk);
    ok = ok && mb_ldscript_write(&ld);
    ok = ok && mb_gen_write(GMK_FILE_NAME, &mk);

    if(ok)
        mb_log(MB_LOG_LEVEL_INFO, "Makefile", "Done: run 'make', 'make flash'");
    else
        mb_log(MB_LOG_LEVEL_ERROR, "Makefile", "Failed to generate, old '%s' is kept", GMK_FILE_NAME);

    free(ld_var);
    mb_ldscript_free(&ld);
    mb_buf_free(&mk);
    mb_gen_vars_free(&v);
    return ok;
}


const struct mb_generator mb_generator_makefile = {
    "makefile",
    GMK_FILE_NAME,
    gmk_generate
};
