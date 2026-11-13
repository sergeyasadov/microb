#ifndef __MICROB_MCU_DB_H__
#define __MICROB_MCU_DB_H__

#include <stddef.h>


#define MB_MCU_DB_MAX_ROOTS 2

struct mcu_db
{
    char* roots[MB_MCU_DB_MAX_ROOTS];
    size_t nroots;
};

void mb_mcu_db_init(struct mcu_db* db);

void mb_mcu_db_free(struct mcu_db* db);

char* mb_mcu_db_find(const struct mcu_db* db, const char* rel);

char* mb_mcu_db_find_mcu(const struct mcu_db* db, const char* name, char** rel_dir);

int mb_mcu_db_valid_name(const char* name);

char* mb_mcu_db_roots_str(const struct mcu_db* db);

#endif // __MICROB_MCU_DB_H__
