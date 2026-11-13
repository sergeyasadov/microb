#ifndef __MICROB_TEMPLATE_H__
#define __MICROB_TEMPLATE_H__

#include "config.h"
#include "utils.h"

int mb_template_render(
    const struct config* cfg,
    const struct config_kv* extra,
    const char* path,
    struct mb_buf* out
);

#endif // __MICROB_TEMPLATE_H__
