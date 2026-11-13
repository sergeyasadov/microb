#ifndef CONFIG_H
#define CONFIG_H

#include "ini.h"
#include "mcu_db.h"
#include "utils.h"

#include <stddef.h>


#define MB_CONFIG_MAX_FAMILY_DEPTH 8
#define MB_CONFIG_MAX_LAYERS (MB_CONFIG_MAX_FAMILY_DEPTH + 3)

struct config_layer
{
    char* name;
    char* reldir;
    struct ini ini;
};

struct config_var
{
    const char* key;
    const char* value;
    int layer;
};

struct config
{
    char* path;
    char* mcu;
    struct mcu_db db;
    struct config_layer layers[MB_CONFIG_MAX_LAYERS];
    size_t nlayers;
    struct config_var* vars;
    size_t nvars;
};

struct config_kv
{
    const char* name;
    const char* value;
};

struct config* mb_config_init(const char* path);

int mb_config_parse(struct config** cfg);

void mb_config_free(struct config** cfg);

const struct config_var* mb_config_find(const struct config* cfg, const char* key);

const char* mb_config_get(const struct config* cfg, const char* key);

char* mb_config_var_name(const char* key);

int mb_config_is_reserved_var(const char* name);

int mb_config_expand(
    const struct config* cfg,
    const struct config_kv* extra,
    const char* s,
    struct mb_buf* out,
    struct mb_buf* err);

#endif // CONFIG_H
