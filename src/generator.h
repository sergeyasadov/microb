#ifndef __MICROB_GENERATOR_H__
#define __MICROB_GENERATOR_H__

#include "config.h"
#include "utils.h"

#include <stddef.h>


struct mb_generator
{
    const char* name;
    const char* output;
    int (*generate)(const struct config* cfg);
};

extern const struct mb_generator mb_generator_makefile;
extern const struct mb_generator mb_generator_cmake;
extern const struct mb_generator mb_generator_xmake;

const struct mb_generator* const* mb_generator_all(void);

const struct mb_generator* mb_generator_find(const char* name);


#define MB_GEN_MAX_VARS 32

struct mb_gen_vars
{
    struct config_kv kv[MB_GEN_MAX_VARS + 1];
    size_t n;
};

void mb_gen_vars_init(struct mb_gen_vars* v);
void mb_gen_vars_set(struct mb_gen_vars* v, const char* name, const char* value, int literal);
void mb_gen_vars_free(struct mb_gen_vars* v);

int mb_gen_expand(const struct config* cfg, const struct mb_gen_vars* v, const char* key, struct mb_buf* out);

char** mb_gen_args(const struct config* cfg, const struct mb_gen_vars* v, const char* key, size_t* n, int* ok);


struct mb_ldscript
{
    char* path;
    int generated;
    struct mb_buf text;
};

int mb_ldscript_prepare(const struct config* cfg, struct mb_ldscript* ld);
int mb_ldscript_write(const struct mb_ldscript* ld);
void mb_ldscript_free(struct mb_ldscript* ld);


int mb_gen_check_extra_sources(const struct config* cfg, const struct mb_gen_vars* v);

int mb_gen_write(const char* path, const struct mb_buf* text);


enum mb_flag_kind
{
    MB_FLAG_OPTION,
    MB_FLAG_DEFINE,
    MB_FLAG_INCLUDE,
    MB_FLAG_LIB
};

enum mb_flag_kind mb_gen_flag(char* const* args, size_t n, size_t* i, const char** value);

#endif // __MICROB_GENERATOR_H__
