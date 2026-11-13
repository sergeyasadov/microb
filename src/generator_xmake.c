#include "generator.h"
#include "logger.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define GXM_FILE_NAME "xmake.lua"

#define GXM_EXPR_BEGIN '\x01'
#define GXM_EXPR_END   '\x02'


static
void
gxm_lua_quote(struct mb_buf* b, const char* s, size_t n)
{
    mb_buf_putc(b, '"');
    for(size_t i = 0; i < n; i++)
    {
        const unsigned char c = (unsigned char)s[i];
        if(c == '\\' || c == '"') mb_buf_printf(b, "\\%c", c);
        else if(c == '\n') mb_buf_puts(b, "\\n");
        else if(c < 0x20 || c == 0x7f) mb_buf_printf(b, "\\%03u", c);
        else mb_buf_putc(b, (char)c);
    }
    mb_buf_putc(b, '"');
}


static
void
gxm_lua_value(struct mb_buf* b, const char* s)
{
    if(*s == '\0')
    {
        mb_buf_puts(b, "\"\"");
        return;
    }

    int first = 1;
    while(*s)
    {
        if(!first) mb_buf_puts(b, " .. ");
        first = 0;

        const char* begin = strchr(s, GXM_EXPR_BEGIN);
        if(begin == s)
        {
            const char* end = strchr(s, GXM_EXPR_END);
            if(!end) end = s + strlen(s);
            mb_buf_putn(b, s + 1, (size_t)(end - s - 1));
            s = *end ? end + 1 : end;
            continue;
        }

        const size_t n = begin ? (size_t)(begin - s) : strlen(s);
        gxm_lua_quote(b, s, n);
        s += n;
    }
}


struct gxm
{
    const struct config* cfg;
    struct mb_gen_vars vd;
    struct mb_gen_vars vs;
    struct mb_buf out;
    struct mb_buf name;
    int ok;
};


static
char*
gxm_expand(struct gxm* g, const struct mb_gen_vars* v, const char* key)
{
    struct mb_buf value;
    mb_buf_init(&value);
    if(!mb_gen_expand(g->cfg, v, key, &value)) g->ok = 0;
    char* s = mb_strdup(mb_buf_str(&value));
    mb_buf_free(&value);
    return s;
}


static
void
gxm_call(struct mb_buf* b, const char* api, char* const* args, size_t n, const char* opts)
{
    if(n == 0) return;

    mb_buf_printf(b, "    %s(", api);
    for(size_t i = 0; i < n; i++)
    {
        if(i) mb_buf_puts(b, ", ");
        gxm_lua_value(b, args[i]);
    }
    if(opts) mb_buf_printf(b, ", %s", opts);
    mb_buf_puts(b, ")\n");
}


struct gxm_list
{
    char** v;
    size_t n;
};


static
void
gxm_list_add(struct gxm_list* l, const char* s)
{
    l->v = (char**)mb_xrealloc(l->v, (l->n + 1) * sizeof(char*));
    l->v[l->n++] = mb_strdup(s);
}


static
void
gxm_toolchain(struct gxm* g)
{
    char* cc = gxm_expand(g, &g->vd, "compiler");
    char* cxx = gxm_expand(g, &g->vd, "cxx");

    mb_buf_puts(&g->out, "toolchain(\"microb\")\n    set_kind(\"standalone\")\n");

    const char* const tools[][2] = {
        {"cc", cc}, {"as", cc}, {"cxx", *cxx ? cxx : NULL}, {"ld", *cxx ? cxx : cc}
    };
    for(size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++)
    {
        if(!tools[i][1]) continue;
        mb_buf_printf(&g->out, "    set_toolset(\"%s\", ", tools[i][0]);
        gxm_lua_quote(&g->out, tools[i][1], strlen(tools[i][1]));
        mb_buf_puts(&g->out, ")\n");
    }

    mb_buf_puts(&g->out, "    on_check(function (toolchain)\n        if not import(\"lib.detect.find_program\")(");
    gxm_lua_quote(&g->out, cc, strlen(cc));
    mb_buf_puts(&g->out, ") then\n            raise(\"microb: compiler \" .. ");
    gxm_lua_quote(&g->out, cc, strlen(cc));
    mb_buf_puts(&g->out, " .. \" not found, add it to PATH\")\n        end\n        return true\n    end)\ntoolchain_end()\n\n");

    free(cc);
    free(cxx);
}


static
void
gxm_options(struct gxm* g)
{
    const struct config* cfg = g->cfg;

    for(size_t i = 0; i < cfg->nflash_reqs; i++)
    {
        const char* key = cfg->flash_reqs[i].key;
        char* value = gxm_expand(g, &g->vd, key);

        mb_buf_printf(&g->out, "option(\"%s\")\n    set_default(", key);
        gxm_lua_quote(&g->out, value, strlen(value));
        mb_buf_printf(&g->out,
            ")\n"
            "    set_showmenu(true)\n"
            "    set_description(\"microb: '%s' for 'xmake build flash'\")\n"
            "option_end()\n\n",
            key);
        free(value);

        char* var = mb_config_var_name(key);
        if(var)
        {
            char expr[128];
            snprintf(expr, sizeof(expr), "%c%s%c", GXM_EXPR_BEGIN, var, GXM_EXPR_END);
            mb_gen_vars_set(&g->vs, var, expr, 1);
            free(var);
        }
    }
}


static
void
gxm_script_locals(struct gxm* g, int check)
{
    const struct config* cfg = g->cfg;
    const char* const name = mb_buf_str(&g->name);

    mb_buf_printf(&g->out,
        "        local fw = target:dep(\"%s\")\n"
        "        local elf = fw:targetfile()\n"
        "        local image = path.join(fw:targetdir(), \"%s.\" .. ",
        name, name);

    char* ext = gxm_expand(g, &g->vd, "output_ext");
    gxm_lua_quote(&g->out, ext, strlen(ext));
    free(ext);
    mb_buf_puts(&g->out, ")\n");

    for(size_t i = 0; check && i < cfg->nflash_reqs; i++)
    {
        const char* key = cfg->flash_reqs[i].key;
        char* var = mb_config_var_name(key);
        if(!var) continue;

        mb_buf_printf(&g->out,
            "        local %s = get_config(\"%s\") or \"\"\n"
            "        if %s == \"\" then\n"
            "            raise(\"microb: %s is not set - set '%s' in microb.ini or run 'xmake f --%s=...'\")\n"
            "        end\n",
            var, key, var, var, key, key);
        free(var);
    }
}


static
int
gxm_exec(struct gxm* g, const char* tool_key, const char* args_key, const char* last)
{
    size_t n = 0;
    char** args = mb_gen_args(g->cfg, &g->vs, args_key, &n, &g->ok);

    char* tool = NULL;
    if(tool_key)
    {
        tool = gxm_expand(g, &g->vs, tool_key);
    }
    else if(n > 0)
    {
        tool = args[0];
        memmove(args, args + 1, (n - 1) * sizeof(char*));
        n--;
    }

    if(!tool || !*tool)
    {
        free(tool);
        mb_strv_free(args, n);
        return 0;
    }

    mb_buf_puts(&g->out, "        os.execv(");
    gxm_lua_value(&g->out, tool);
    mb_buf_puts(&g->out, ", {");
    for(size_t i = 0; i < n; i++)
    {
        if(i) mb_buf_puts(&g->out, ", ");
        gxm_lua_value(&g->out, args[i]);
    }
    if(last) mb_buf_printf(&g->out, "%s%s", n ? ", " : "", last);
    mb_buf_puts(&g->out, "})\n");

    free(tool);
    mb_strv_free(args, n);
    return 1;
}


static
void
gxm_firmware(struct gxm* g)
{
    const char* const name = mb_buf_str(&g->name);

    mb_buf_printf(&g->out,
        "target(\"%s\")\n"
        "    set_kind(\"binary\")\n"
        "    set_plat(\"cross\")\n"
        "    set_arch(\"%s\")\n"
        "    set_toolchains(\"microb\")\n"
        "    set_extension(\".elf\")\n",
        name, g->cfg->mcu);

    char* src = gxm_expand(g, &g->vd, "source_dir");
    static const char* const exts[] = {"c", "cpp", "s", "S"};
    mb_buf_puts(&g->out, "    for _, pattern in ipairs({");
    for(size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
    {
        struct mb_buf glob;
        mb_buf_init(&glob);
        mb_buf_printf(&glob, "%s/*.%s", src, exts[i]);
        if(i) mb_buf_puts(&g->out, ", ");
        gxm_lua_value(&g->out, mb_buf_str(&glob));
        mb_buf_free(&glob);
    }
    mb_buf_puts(&g->out,
        "}) do\n"
        "        if #os.files(pattern) > 0 then add_files(pattern) end\n"
        "    end\n");
    free(src);

    struct gxm_list files = {NULL, 0};
    size_t nextra = 0;
    char** extra = mb_gen_args(g->cfg, &g->vd, "extra_sources", &nextra, &g->ok);
    for(size_t i = 0; i < nextra; i++) gxm_list_add(&files, extra[i]);
    mb_strv_free(extra, nextra);

    gxm_call(&g->out, "add_files", files.v, files.n, NULL);
    mb_strv_free(files.v, files.n);

    struct gxm_list defs = {NULL, 0}, incs = {NULL, 0}, opts = {NULL, 0};

    char* include_dir = gxm_expand(g, &g->vd, "include_dir");
    if(*include_dir) gxm_list_add(&incs, include_dir);
    free(include_dir);

    static const char* const common[] = {"arch_flags", "defines", "includes"};
    for(size_t k = 0; k < sizeof(common) / sizeof(common[0]); k++)
    {
        size_t n = 0;
        char** args = mb_gen_args(g->cfg, &g->vd, common[k], &n, &g->ok);
        for(size_t i = 0; i < n;)
        {
            const char* value;
            switch(mb_gen_flag(args, n, &i, &value))
            {
                case MB_FLAG_DEFINE:  gxm_list_add(&defs, value); break;
                case MB_FLAG_INCLUDE: gxm_list_add(&incs, value); break;
                default:              gxm_list_add(&opts, value); break;
            }
        }
        mb_strv_free(args, n);
    }

    gxm_call(&g->out, "add_includedirs", incs.v, incs.n, NULL);
    gxm_call(&g->out, "add_defines", defs.v, defs.n, NULL);
    gxm_call(&g->out, "add_cxflags", opts.v, opts.n, "{force = true}");
    gxm_call(&g->out, "add_asflags", opts.v, opts.n, "{force = true}");
    mb_strv_free(defs.v, defs.n);
    mb_strv_free(incs.v, incs.n);
    mb_strv_free(opts.v, opts.n);

    static const char* const per_lang[][2] = {
        {"cflags", "add_cflags"},
        {"cppflags", "add_cxxflags"},
        {"asflags", "add_asflags"},
    };
    for(size_t k = 0; k < sizeof(per_lang) / sizeof(per_lang[0]); k++)
    {
        size_t n = 0;
        char** args = mb_gen_args(g->cfg, &g->vd, per_lang[k][0], &n, &g->ok);
        gxm_call(&g->out, per_lang[k][1], args, n, "{force = true}");
        mb_strv_free(args, n);
    }

    struct gxm_list lopts = {NULL, 0}, libs = {NULL, 0};
    static const char* const link[] = {"arch_flags", "link_flags", "ldflags"};
    for(size_t k = 0; k < sizeof(link) / sizeof(link[0]); k++)
    {
        size_t n = 0;
        char** args = mb_gen_args(g->cfg, &g->vd, link[k], &n, &g->ok);
        for(size_t i = 0; i < n;)
        {
            const size_t start = i;
            const char* value;
            if(mb_gen_flag(args, n, &i, &value) == MB_FLAG_LIB) gxm_list_add(&libs, value);
            else for(size_t j = start; j < i; j++) gxm_list_add(&lopts, args[j]);
        }
        mb_strv_free(args, n);
    }
    gxm_call(&g->out, "add_ldflags", lopts.v, lopts.n, "{force = true}");
    gxm_call(&g->out, "add_links", libs.v, libs.n, NULL);
    mb_strv_free(lopts.v, lopts.n);
    mb_strv_free(libs.v, libs.n);

    char* ext = gxm_expand(g, &g->vd, "output_ext");
    mb_buf_printf(&g->out,
        "\n"
        "    after_build(function (target)\n"
        "        local elf = target:targetfile()\n"
        "        local image = path.join(target:targetdir(), \"%s.\" .. ",
        name);
    gxm_lua_quote(&g->out, ext, strlen(ext));
    mb_buf_puts(&g->out, ")\n");
    free(ext);

    if(!gxm_exec(g, "objcopy", "objcopy_flags", "elf, image"))
        mb_buf_puts(&g->out, "        raise(\"microb: 'objcopy' is not set for this MCU\")\n");
    mb_buf_puts(&g->out, "        print(\"microb: \" .. image)\n    end)\ntarget_end()\n\n");
}


static
void
gxm_phony(struct gxm* g, const char* target, int check)
{
    mb_buf_printf(&g->out,
        "target(\"%s\")\n"
        "    set_kind(\"phony\")\n"
        "    set_default(false)\n"
        "    add_deps(\"%s\")\n"
        "    on_build(function (target)\n",
        target, mb_buf_str(&g->name));

    gxm_script_locals(g, check);
}


static
void
gxm_utility_targets(struct gxm* g)
{
    gxm_phony(g, "size", 0);
    if(!gxm_exec(g, "size", "size_flags", "elf"))
        mb_buf_puts(&g->out, "        raise(\"microb: 'size' is not set for this MCU\")\n");
    mb_buf_puts(&g->out, "    end)\ntarget_end()\n\n");

    gxm_phony(g, "flash", 1);
    if(!mb_config_get(g->cfg, "upload_cmd") || !gxm_exec(g, NULL, "upload_cmd", NULL))
        mb_buf_puts(&g->out, "        raise(\"microb: upload_cmd is not set, choose 'upload' in microb.ini\")\n");
    mb_buf_puts(&g->out, "    end)\ntarget_end()\n");
}


static
int
gxm_valid_name(const char* s)
{
    if(*s == '\0') return 0;
    for(; *s; s++)
        if(!(isalnum((unsigned char)*s) || *s == '_' || *s == '-' || *s == '.')) return 0;
    return 1;
}


static
int
gxm_generate(const struct config* cfg)
{
    struct gxm g;
    memset(&g, 0, sizeof(g));
    g.cfg = cfg;
    g.ok = 1;
    mb_gen_vars_init(&g.vd);
    mb_gen_vars_init(&g.vs);
    mb_buf_init(&g.out);
    mb_buf_init(&g.name);

    struct mb_ldscript ld;
    g.ok = mb_ldscript_prepare(cfg, &ld);
    g.ok = g.ok && mb_gen_check_extra_sources(cfg, NULL);

    if(g.ok && mb_gen_expand(cfg, NULL, "project_name", &g.name) && !gxm_valid_name(mb_buf_str(&g.name)))
    {
        mb_log(MB_LOG_LEVEL_ERROR, "xmake", "project_name '%s' can't be an xmake target name (use letters, digits, _ - .)",
            mb_buf_str(&g.name));
        g.ok = 0;
    }

    if(g.ok)
    {
        struct mb_gen_vars* const both[] = {&g.vd, &g.vs};
        for(size_t i = 0; i < 2; i++)
        {
            mb_gen_vars_set(both[i], "PROJECT_BUILD_DIR", "$(builddir)", 1);
            mb_gen_vars_set(both[i], "CURDIR", "$(projectdir)", 1);
            if(ld.path) mb_gen_vars_set(both[i], "LINKER_SCRIPT", ld.path, 1);
        }

        mb_gen_vars_set(&g.vd, "TARGET", "$(builddir)/firmware", 1);
        mb_gen_vars_set(&g.vd, "ELF", "$(builddir)/firmware.elf", 1);
        mb_gen_vars_set(&g.vs, "TARGET", "\x01image\x02", 1);
        mb_gen_vars_set(&g.vs, "ELF", "\x01" "elf\x02", 1);

        mb_buf_printf(&g.out,
            "set_xmakever(\"3.0.0\")\n"
            "set_project(\"%s\")\n"
            "add_rules(\"plugin.compile_commands.autoupdate\", {outputdir = \".\"})\n\n",
            mb_buf_str(&g.name));

        gxm_toolchain(&g);
        gxm_options(&g);
        gxm_firmware(&g);
        gxm_utility_targets(&g);
    }

    int ok = g.ok;
    ok = ok && mb_ldscript_write(&ld);
    ok = ok && mb_gen_write(GXM_FILE_NAME, &g.out);

    if(ok)
        mb_log(MB_LOG_LEVEL_INFO, "xmake", "Done: run 'xmake', 'xmake build flash'");
    else
        mb_log(MB_LOG_LEVEL_ERROR, "xmake", "Failed to generate, old '%s' is kept", GXM_FILE_NAME);

    mb_ldscript_free(&ld);
    mb_gen_vars_free(&g.vd);
    mb_gen_vars_free(&g.vs);
    mb_buf_free(&g.out);
    mb_buf_free(&g.name);
    return ok;
}


const struct mb_generator mb_generator_xmake = {
    "xmake",
    GXM_FILE_NAME,
    gxm_generate
};
