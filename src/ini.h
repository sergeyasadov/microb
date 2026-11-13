#ifndef __MICROB_INI_H__
#define __MICROB_INI_H__

#include <stddef.h>


struct ini_section
{
    char* key;
    char* value;
};

struct ini_entry
{
    char* key;
    char* value;
    int section;
    int line;
    int append; /* 'key += value' */
};

struct ini
{
    char* path;
    struct ini_entry* entries;
    size_t nentries;
    struct ini_section* sections;
    size_t nsections;
};

void mb_ini_init(struct ini* ini, const char* path);

int mb_ini_load(struct ini* ini, const char* path);

void mb_ini_add(struct ini* ini, int section, const char* key, const char* value, int line);

void mb_ini_free(struct ini* ini);

int mb_ini_normalize_key(char* key);

#endif // __MICROB_INI_H__
