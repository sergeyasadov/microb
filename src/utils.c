#define _POSIX_C_SOURCE 200809L

#include "utils.h"
#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>


void*
mb_xmalloc(size_t size)
{
    void* p = malloc(size ? size : 1);
    if(!p)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Memory", "Out of memory");
        exit(EXIT_FAILURE);
    }
    return p;
}


void*
mb_xrealloc(void* p, size_t size)
{
    void* n = realloc(p, size ? size : 1);
    if(!n)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "Memory", "Out of memory");
        exit(EXIT_FAILURE);
    }
    return n;
}


char*
mb_strndup(const char* s, size_t n)
{
    char* copy = (char*)mb_xmalloc(n + 1);
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}


char*
mb_strdup(const char* s)
{
    return mb_strndup(s, strlen(s));
}


char*
mb_trim(char* s)
{
    while(isspace((unsigned char)*s)) s++;

    char* end = s + strlen(s);
    while(end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';

    return s;
}


int
mb_strcaseeq(const char* a, const char* b)
{
    for(; *a && *b; a++, b++)
    {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    }
    return *a == *b;
}


void
mb_str_lower(char* s)
{
    for(; *s; s++) *s = (char)tolower((unsigned char)*s);
}


char*
mb_path_join(const char* a, const char* b)
{
    const size_t la = strlen(a);
    const size_t lb = strlen(b);
    const int slash = la > 0 && a[la - 1] != '/';

    char* p = (char*)mb_xmalloc(la + (size_t)slash + lb + 1);
    memcpy(p, a, la);
    if(slash) p[la] = '/';
    memcpy(p + la + (size_t)slash, b, lb + 1);
    return p;
}


char*
mb_dirname(const char* path)
{
    const char* slash = strrchr(path, '/');
    if(!slash) return mb_strdup(".");
    if(slash == path) return mb_strdup("/");
    return mb_strndup(path, (size_t)(slash - path));
}


int
mb_is_dir(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


int
mb_is_file(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


int
mb_mkdir_p(const char* path)
{
    if(*path == '\0') return 0;

    char* p = mb_strdup(path);
    int ok = 1;

    for(char* s = p + 1; ok; s++)
    {
        const char c = *s;
        if(c != '/' && c != '\0') continue;

        *s = '\0';
        if(mkdir(p, 0777) != 0 && errno != EEXIST) ok = 0;
        *s = c;

        if(c == '\0') break;
    }

    ok = ok && mb_is_dir(path);
    free(p);
    return ok;
}


void
mb_buf_init(struct mb_buf* b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}


void
mb_buf_free(struct mb_buf* b)
{
    free(b->data);
    mb_buf_init(b);
}


void
mb_buf_reset(struct mb_buf* b)
{
    b->len = 0;
    if(b->data) b->data[0] = '\0';
}


static
void
mb_buf_reserve(struct mb_buf* b, size_t extra)
{
    const size_t need = b->len + extra + 1;
    if(need <= b->cap) return;

    size_t cap = b->cap ? b->cap : 64;
    while(cap < need) cap *= 2;

    b->data = (char*)mb_xrealloc(b->data, cap);
    b->cap = cap;
}


void
mb_buf_putn(struct mb_buf* b, const char* s, size_t n)
{
    mb_buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}


void
mb_buf_puts(struct mb_buf* b, const char* s)
{
    mb_buf_putn(b, s, strlen(s));
}


void
mb_buf_putc(struct mb_buf* b, char c)
{
    mb_buf_putn(b, &c, 1);
}


void
mb_buf_printf(struct mb_buf* b, const char* fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    const int n = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if(n <= 0) return;

    mb_buf_reserve(b, (size_t)n);

    va_start(args, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, args);
    va_end(args);

    b->len += (size_t)n;
}


const char*
mb_buf_str(const struct mb_buf* b)
{
    return b->data ? b->data : "";
}


int
mb_read_line(FILE* f, struct mb_buf* line)
{
    mb_buf_reset(line);
    mb_buf_putn(line, "", 0);

    int c;
    int any = 0;
    while((c = fgetc(f)) != EOF)
    {
        any = 1;
        if(c == '\n') break;
        mb_buf_putc(line, (char)c);
    }

    if(ferror(f)) return -1;
    if(!any) return 0;

    if(line->len > 0 && line->data[line->len - 1] == '\r')
        line->data[--line->len] = '\0';

    return 1;
}


int
mb_file_same(const char* path, const char* data, size_t len)
{
    FILE* f = fopen(path, "rb");
    if(!f) return 0;

    char chunk[4096];
    size_t pos = 0;
    int same = 1;
    size_t n;

    while(same && (n = fread(chunk, 1, sizeof(chunk), f)) > 0)
    {
        same = pos + n <= len && memcmp(chunk, data + pos, n) == 0;
        pos += n;
    }

    same = same && !ferror(f) && pos == len;
    fclose(f);
    return same;
}


int
mb_write_file_atomic(const char* path, const char* data, size_t len)
{
    char* tmp = (char*)mb_xmalloc(strlen(path) + sizeof(".microb-tmp"));
    strcpy(tmp, path);
    strcat(tmp, ".microb-tmp");

    FILE* f = fopen(tmp, "wb");
    if(!f)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "File", "Can't open '%s' for writing: %s", tmp, strerror(errno));
        free(tmp);
        return 0;
    }

    int ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;

    if(ok && rename(tmp, path) != 0)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "File", "Can't rename '%s' to '%s': %s", tmp, path, strerror(errno));
        ok = 0;
    }
    else if(!ok)
    {
        mb_log(MB_LOG_LEVEL_ERROR, "File", "Can't write '%s'", tmp);
    }

    if(!ok) remove(tmp);

    free(tmp);
    return ok;
}
