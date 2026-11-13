#include "generator.h"
#include "logger.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define GCM_FILE_NAME "CMakeLists.txt"


static
void
gcm_quote(struct mb_buf* b, const char* prefix, const char* s, const char* suffix)
{
    mb_buf_putc(b, '"');
    mb_buf_puts(b, prefix);
    for(const char* c = s; *c; c++)
    {
        if(*prefix && *c == '>')
        {
            mb_buf_puts(b, "$<ANGLE-R>");
            continue;
        }
        if(*c == '\\' || *c == '"' || *c == ';') mb_buf_putc(b, '\\');
        mb_buf_putc(b, *c);
    }
    mb_buf_puts(b, suffix);
    mb_buf_putc(b, '"');
}


static
void
gcm_line(struct mb_buf* b, const char* prefix, const char* s, const char* suffix)
{
    mb_buf_puts(b, "    ");
    gcm_quote(b, prefix, s, suffix);
    mb_buf_putc(b, '\n');
}


static
int
gcm_valid_name(const char* s)
{
    if(*s == '\0') return 0;
    for(; *s; s++)
        if(!(isalnum((unsigned char)*s) || *s == '_' || *s == '-' || *s == '.' || *s == '+')) return 0;
    return 1;
}


static
void
gcm_path(struct mb_buf* b, const char* path)
{
    mb_buf_puts(b, "    ");
    if(mb_is_abs_path(path))
        gcm_quote(b, "", path, "");
    else
        gcm_quote(b, "${CMAKE_CURRENT_SOURCE_DIR}/", path, "");
    mb_buf_putc(b, '\n');
}


struct gcm
{
    const struct config* cfg;
    struct mb_gen_vars v;
    struct mb_buf out;
    struct mb_buf name;
    char* target;
    int ok;
};


static
char*
gcm_expand(struct gcm* g, const char* key)
{
    struct mb_buf value;
    mb_buf_init(&value);
    if(!mb_gen_expand(g->cfg, &g->v, key, &value)) g->ok = 0;
    char* s = mb_strdup(mb_buf_str(&value));
    mb_buf_free(&value);
    return s;
}


static
void
gcm_toolchain(struct gcm* g, int cxx)
{
    char* cc = gcm_expand(g, "compiler");
    char* cxx_cmd = gcm_expand(g, "cxx");

    mb_buf_puts(&g->out,
        "cmake_minimum_required(VERSION 3.20)\n"
        "\n"
        "set(CMAKE_SYSTEM_NAME Generic)\n");

    mb_buf_puts(&g->out, "set(CMAKE_C_COMPILER ");
    gcm_quote(&g->out, "", cc, "");
    mb_buf_puts(&g->out, ")\nset(CMAKE_ASM_COMPILER ");
    gcm_quote(&g->out, "", cc, "");
    mb_buf_puts(&g->out, ")\n");

    if(cxx)
    {
        mb_buf_puts(&g->out, "set(CMAKE_CXX_COMPILER ");
        gcm_quote(&g->out, "", cxx_cmd, "");
        mb_buf_puts(&g->out, ")\n");
    }

    mb_buf_puts(&g->out,
        "set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n"
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        "\n");

    mb_buf_printf(&g->out, "project(%s C %sASM)\n\n", mb_buf_str(&g->name), cxx ? "CXX " : "");

    free(cc);
    free(cxx_cmd);
}


static
void
gcm_flash_vars(struct gcm* g)
{
    const struct config* cfg = g->cfg;
    if(cfg->nflash_reqs == 0) return;


    for(size_t i = 0; i < cfg->nflash_reqs; i++)
    {
        char* var = mb_config_var_name(cfg->flash_reqs[i].key);
        if(!var) continue;

        char* value = gcm_expand(g, cfg->flash_reqs[i].key);
        mb_buf_printf(&g->out, "if(NOT DEFINED %s)\n    set(%s ", var, var);
        gcm_quote(&g->out, "", value, "");
        mb_buf_puts(&g->out, ")\nendif()\n");
        free(value);
        free(var);
    }
    mb_buf_putc(&g->out, '\n');

    for(size_t i = 0; i < cfg->nflash_reqs; i++)
    {
        char* var = mb_config_var_name(cfg->flash_reqs[i].key);
        if(!var) continue;

        struct mb_buf ref;
        mb_buf_init(&ref);
        mb_buf_printf(&ref, "${%s}", var);
        mb_gen_vars_set(&g->v, var, mb_buf_str(&ref), 1);
        mb_buf_free(&ref);
        free(var);
    }
}


static
void
gcm_sources(struct gcm* g, int cxx)
{
    char* src = gcm_expand(g, "source_dir");
    const char* const name = mb_buf_str(&g->name);

    static const char* const exts[] = {"c", "s", "S", "cpp"};
    const size_t nexts = cxx ? 4 : 3;

    mb_buf_puts(&g->out, "file(GLOB MICROB_SOURCES CONFIGURE_DEPENDS\n");
    for(size_t i = 0; i < nexts; i++)
    {
        struct mb_buf glob;
        mb_buf_init(&glob);
        mb_buf_printf(&glob, "%s/*.%s", src, exts[i]);
        gcm_path(&g->out, mb_buf_str(&glob));
        mb_buf_free(&glob);
    }
    mb_buf_puts(&g->out, ")\nlist(REMOVE_DUPLICATES MICROB_SOURCES)\n\n");

    if(!cxx)
    {
        mb_buf_puts(&g->out, "file(GLOB MICROB_CXX_SOURCES ");
        gcm_quote(&g->out, "${CMAKE_CURRENT_SOURCE_DIR}/", src, "/*.cpp");
        mb_buf_puts(&g->out,
            ")\n"
            "if(MICROB_CXX_SOURCES)\n"
            "    message(FATAL_ERROR \"microb: 'cxx' is not set for this MCU, C++ sources can't be built\")\n"
            "endif()\n\n");
    }

    size_t n = 0;
    char** extra = mb_gen_args(g->cfg, &g->v, "extra_sources", &n, &g->ok);

    mb_buf_printf(&g->out, "add_executable(%s ${MICROB_SOURCES}", name);
    if(n) mb_buf_putc(&g->out, '\n');
    for(size_t i = 0; i < n; i++) gcm_path(&g->out, extra[i]);
    mb_buf_printf(&g->out, ")\nset_target_properties(%s PROPERTIES SUFFIX \".elf\")\n\n", name);

    mb_strv_free(extra, n);
    free(src);
}


static
void
gcm_compile_flags(struct gcm* g)
{
    const char* const name = mb_buf_str(&g->name);

    struct mb_buf defs, incs, opts;
    mb_buf_init(&defs);
    mb_buf_init(&incs);
    mb_buf_init(&opts);

    char* include_dir = gcm_expand(g, "include_dir");
    if(*include_dir) gcm_path(&incs, include_dir);
    free(include_dir);

    static const char* const common[] = {"arch_flags", "defines", "includes"};
    for(size_t k = 0; k < sizeof(common) / sizeof(common[0]); k++)
    {
        size_t n = 0;
        char** args = mb_gen_args(g->cfg, &g->v, common[k], &n, &g->ok);

        for(size_t i = 0; i < n;)
        {
            const char* value;
            switch(mb_gen_flag(args, n, &i, &value))
            {
                case MB_FLAG_DEFINE:  gcm_line(&defs, "", value, ""); break;
                case MB_FLAG_INCLUDE: gcm_path(&incs, value); break;
                default:              gcm_line(&opts, "", value, ""); break;
            }
        }
        mb_strv_free(args, n);
    }

    static const struct { const char* key; const char* lang; } per_lang[] = {
        {"cflags", "C"},
        {"cppflags", "CXX"},
        {"asflags", "ASM"},
    };
    for(size_t k = 0; k < sizeof(per_lang) / sizeof(per_lang[0]); k++)
    {
        size_t n = 0;
        char** args = mb_gen_args(g->cfg, &g->v, per_lang[k].key, &n, &g->ok);

        char prefix[48];
        snprintf(prefix, sizeof(prefix), "$<$<COMPILE_LANGUAGE:%s>:", per_lang[k].lang);
        for(size_t i = 0; i < n; i++) gcm_line(&opts, prefix, args[i], ">");

        mb_strv_free(args, n);
    }

    if(incs.len) mb_buf_printf(&g->out, "target_include_directories(%s PRIVATE\n%s)\n", name, mb_buf_str(&incs));
    if(defs.len) mb_buf_printf(&g->out, "target_compile_definitions(%s PRIVATE\n%s)\n", name, mb_buf_str(&defs));
    if(opts.len) mb_buf_printf(&g->out, "target_compile_options(%s PRIVATE\n%s)\n", name, mb_buf_str(&opts));
    mb_buf_putc(&g->out, '\n');

    mb_buf_free(&defs);
    mb_buf_free(&incs);
    mb_buf_free(&opts);
}


static
void
gcm_link_flags(struct gcm* g, const struct mb_ldscript* ld)
{
    const char* const name = mb_buf_str(&g->name);

    struct mb_buf opts, libs;
    mb_buf_init(&opts);
    mb_buf_init(&libs);

    static const char* const keys[] = {"arch_flags", "link_flags", "ldflags"};
    for(size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++)
    {
        size_t n = 0;
        char** args = mb_gen_args(g->cfg, &g->v, keys[k], &n, &g->ok);

        for(size_t i = 0; i < n;)
        {
            const size_t start = i;
            const char* value;
            if(mb_gen_flag(args, n, &i, &value) == MB_FLAG_LIB)
                gcm_line(&libs, "", value, "");
            else
                for(size_t j = start; j < i; j++) gcm_line(&opts, "", args[j], "");
        }
        mb_strv_free(args, n);
    }

    if(opts.len) mb_buf_printf(&g->out, "target_link_options(%s PRIVATE\n%s)\n", name, mb_buf_str(&opts));
    if(libs.len) mb_buf_printf(&g->out, "target_link_libraries(%s PRIVATE\n%s)\n", name, mb_buf_str(&libs));
    mb_buf_putc(&g->out, '\n');

    if(ld->path)
    {
        mb_buf_puts(&g->out, "set(MICROB_LINKER_SCRIPT ");
        gcm_quote(&g->out, mb_is_abs_path(ld->path) ? "" : "${CMAKE_CURRENT_SOURCE_DIR}/", ld->path, "");
        mb_buf_printf(&g->out,
            ")\n"
            "if(NOT EXISTS \"${MICROB_LINKER_SCRIPT}\")\n"
            "    message(FATAL_ERROR \"microb: ${MICROB_LINKER_SCRIPT} not found, run microb to generate it\")\n"
            "endif()\n"
            "set_target_properties(%s PROPERTIES LINK_DEPENDS \"${MICROB_LINKER_SCRIPT}\")\n\n",
            name);
    }

    mb_buf_free(&opts);
    mb_buf_free(&libs);
}


static
int
gcm_command(struct gcm* g, const char* tool_key, const char* args_key, const char* last)
{
    char* tool = gcm_expand(g, tool_key);
    if(!*tool)
    {
        free(tool);
        return 0;
    }

    mb_buf_puts(&g->out, "    COMMAND ");
    gcm_quote(&g->out, "", tool, "");

    size_t n = 0;
    char** args = args_key ? mb_gen_args(g->cfg, &g->v, args_key, &n, &g->ok) : NULL;
    for(size_t i = 0; i < n; i++)
    {
        mb_buf_putc(&g->out, ' ');
        gcm_quote(&g->out, "", args[i], "");
    }
    mb_strv_free(args, n);

    if(last) mb_buf_printf(&g->out, " %s", last);
    mb_buf_putc(&g->out, '\n');

    free(tool);
    return 1;
}


static
void
gcm_targets(struct gcm* g)
{
    const struct config* cfg = g->cfg;
    const char* const name = mb_buf_str(&g->name);
    const char* const target = g->target;

    mb_buf_printf(&g->out, "add_custom_command(TARGET %s POST_BUILD\n", name);
    struct mb_buf last;
    mb_buf_init(&last);
    mb_buf_printf(&last, "\"$<TARGET_FILE:%s>\" \"%s\"", name, target);
    gcm_command(g, "objcopy", "objcopy_flags", mb_buf_str(&last));
    mb_buf_printf(&g->out, "    BYPRODUCTS \"%s\"\n    VERBATIM)\n\n", target);
    mb_buf_free(&last);

    mb_buf_printf(&g->out, "add_custom_target(size\n");
    mb_buf_init(&last);
    mb_buf_printf(&last, "\"$<TARGET_FILE:%s>\"", name);
    if(!gcm_command(g, "size", "size_flags", mb_buf_str(&last)))
        mb_buf_puts(&g->out,
            "    COMMAND ${CMAKE_COMMAND} -E echo \"microb: 'size' is not set for this MCU\"\n"
            "    COMMAND ${CMAKE_COMMAND} -E false\n");
    mb_buf_printf(&g->out, "    DEPENDS %s\n    VERBATIM)\n\n", name);
    mb_buf_free(&last);

    if(!mb_config_get(cfg, "upload_cmd"))
    {
        mb_buf_puts(&g->out,
            "add_custom_target(flash\n"
            "    COMMAND ${CMAKE_COMMAND} -E echo \"microb: upload_cmd is not set, choose 'upload' in microb.ini\"\n"
            "    COMMAND ${CMAKE_COMMAND} -E false\n"
            "    VERBATIM)\n");
        return;
    }

    mb_buf_puts(&g->out, "set(MICROB_FLASH_MISSING \"\")\n");
    for(size_t i = 0; i < cfg->nflash_reqs; i++)
    {
        char* var = mb_config_var_name(cfg->flash_reqs[i].key);
        if(!var) continue;
        mb_buf_printf(&g->out, "if(\"${%s}\" STREQUAL \"\")\n    list(APPEND MICROB_FLASH_MISSING %s)\nendif()\n", var, var);
        free(var);
    }

    mb_buf_puts(&g->out,
        "if(MICROB_FLASH_MISSING)\n"
        "    list(JOIN MICROB_FLASH_MISSING \", \" MICROB_FLASH_MISSING)\n"
        "    add_custom_target(flash\n"
        "        COMMAND ${CMAKE_COMMAND} -E echo \"microb: ${MICROB_FLASH_MISSING} not set - set it in microb.ini or run cmake -D<NAME>=...\"\n"
        "        COMMAND ${CMAKE_COMMAND} -E false\n"
        "        VERBATIM)\n"
        "else()\n"
        "    add_custom_target(flash\n");

    size_t n = 0;
    char** args = mb_gen_args(cfg, &g->v, "upload_cmd", &n, &g->ok);
    mb_buf_puts(&g->out, "        COMMAND");
    for(size_t i = 0; i < n; i++)
    {
        mb_buf_putc(&g->out, ' ');
        gcm_quote(&g->out, "", args[i], "");
    }
    mb_strv_free(args, n);

    mb_buf_printf(&g->out, "\n        DEPENDS %s\n        USES_TERMINAL\n        VERBATIM)\nendif()\n", name);
}


static
int
gcm_generate(const struct config* cfg)
{
    struct gcm g;
    memset(&g, 0, sizeof(g));
    g.cfg = cfg;
    g.ok = 1;
    mb_gen_vars_init(&g.v);
    mb_buf_init(&g.out);
    mb_buf_init(&g.name);

    struct mb_ldscript ld;
    g.ok = mb_ldscript_prepare(cfg, &ld);
    g.ok = g.ok && mb_gen_check_extra_sources(cfg, NULL);

    if(g.ok && mb_gen_expand(cfg, NULL, "project_name", &g.name) && !gcm_valid_name(mb_buf_str(&g.name)))
    {
        mb_log(MB_LOG_LEVEL_ERROR, "CMake", "project_name '%s' can't be a CMake target name (use letters, digits, _ - . +)",
            mb_buf_str(&g.name));
        g.ok = 0;
    }

    if(g.ok)
    {
        const char* const name = mb_buf_str(&g.name);

        struct mb_buf s;
        mb_buf_init(&s);

        mb_gen_vars_set(&g.v, "PROJECT_BUILD_DIR", "${CMAKE_BINARY_DIR}", 1);
        mb_gen_vars_set(&g.v, "CURDIR", "${CMAKE_CURRENT_SOURCE_DIR}", 1);

        mb_buf_printf(&s, "$<TARGET_FILE:%s>", name);
        mb_gen_vars_set(&g.v, "ELF", mb_buf_str(&s), 1);

        char* ext = gcm_expand(&g, "output_ext");
        mb_buf_reset(&s);
        mb_buf_printf(&s, "${CMAKE_BINARY_DIR}/%s.%s", name, ext);
        mb_gen_vars_set(&g.v, "TARGET", mb_buf_str(&s), 1);
        g.target = mb_strdup(mb_buf_str(&s));
        free(ext);

        if(ld.path)
        {
            mb_buf_reset(&s);
            mb_buf_printf(&s, "%s%s", mb_is_abs_path(ld.path) ? "" : "${CMAKE_CURRENT_SOURCE_DIR}/", ld.path);
            mb_gen_vars_set(&g.v, "LINKER_SCRIPT", mb_buf_str(&s), 1);
        }
        mb_buf_free(&s);

        const int cxx = mb_config_get(cfg, "cxx") != NULL;

        gcm_toolchain(&g, cxx);
        mb_buf_printf(&g.out, "set(MICROB_MCU \"%s\")\n\n", cfg->mcu);
        gcm_flash_vars(&g);
        gcm_sources(&g, cxx);
        gcm_compile_flags(&g);
        gcm_link_flags(&g, &ld);
        gcm_targets(&g);
    }

    int ok = g.ok;
    ok = ok && mb_ldscript_write(&ld);
    ok = ok && mb_gen_write(GCM_FILE_NAME, &g.out);

    if(ok)
        mb_log(MB_LOG_LEVEL_INFO, "CMake", "Done: run 'cmake -B build -G Ninja && cmake --build build'");
    else
        mb_log(MB_LOG_LEVEL_ERROR, "CMake", "Failed to generate, old '%s' is kept", GCM_FILE_NAME);

    mb_ldscript_free(&ld);
    mb_gen_vars_free(&g.v);
    mb_buf_free(&g.out);
    mb_buf_free(&g.name);
    free(g.target);
    return ok;
}


const struct mb_generator mb_generator_cmake = {
    "cmake",
    GCM_FILE_NAME,
    gcm_generate
};
