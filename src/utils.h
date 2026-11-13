#ifndef __MICROB_UTILS_H__
#define __MICROB_UTILS_H__

#include <stddef.h>
#include <stdio.h>


void* mb_xmalloc(size_t size);
void* mb_xrealloc(void* p, size_t size);
char* mb_strdup(const char* s);
char* mb_strndup(const char* s, size_t n);

char* mb_trim(char* s);
int mb_strcaseeq(const char* a, const char* b);
void mb_str_lower(char* s);

char* mb_path_join(const char* a, const char* b);
char* mb_dirname(const char* path);
int mb_is_dir(const char* path);
int mb_is_file(const char* path);
int mb_mkdir_p(const char* path);


struct mb_buf
{
    char* data;
    size_t len;
    size_t cap;
};

void mb_buf_init(struct mb_buf* b);
void mb_buf_free(struct mb_buf* b);
void mb_buf_reset(struct mb_buf* b);
void mb_buf_putn(struct mb_buf* b, const char* s, size_t n);
void mb_buf_puts(struct mb_buf* b, const char* s);
void mb_buf_putc(struct mb_buf* b, char c);
void mb_buf_printf(struct mb_buf* b, const char* fmt, ...);
const char* mb_buf_str(const struct mb_buf* b);


int mb_read_line(FILE* f, struct mb_buf* line);

int mb_file_same(const char* path, const char* data, size_t len);

int mb_write_file_atomic(const char* path, const char* data, size_t len);

#endif // __MICROB_UTILS_H__
