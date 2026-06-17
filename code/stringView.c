//#include <stdio.h>
#include <string.h>
#include <ctype.h>
//#include <stdlib.h>

typedef struct {
    const char *data;
    size_t count;
} String_View;

String_View sv(const char *cstr) {
    return (String_View) {
        .data = cstr,
        .count = cstr != NULL ? strlen(cstr) : 0,
    };
}

void sv_chop_left(String_View *sv, size_t n){
    if (n > sv->count) n = sv->count;
    sv->count -= n;
    sv->data += n;
}

void sv_chop_right(String_View *sv, size_t n) {
    if (n > sv->count) n = sv->count;
    sv->count -= n;
}

void sv_trim_left(String_View *sv) {
    while (sv->count > 0 && isspace(sv->data[0])) {
        sv_chop_left(sv, 1);
    }
}

void sv_trim_right(String_View *sv) {
    while (sv->count > 0 && isspace(sv->data[sv->count - 1])) {
        sv_chop_right(sv, 1);
    }
}

void sv_trim(String_View *sv) {
    sv_trim_left(sv);
    sv_trim_right(sv);
}

String_View sv_chop_by_delim(String_View *sv, char delim) {
    size_t pos = 0;
    while (pos < sv->count && sv->data[pos] != delim) pos++;

    if (pos < sv->count) {
        String_View result = {
            .data = sv->data,
            .count = pos,
        };
        sv_chop_left(sv, pos+1);
        return result;
    }

    String_View result = *sv;
    sv_chop_left(sv, sv->count);
    return result;
}

String_View sv_chop_by_type(String_View *sv, int (*istype)(int c)) {
    size_t pos = 0;
    while (pos < sv->count && !istype(sv->data[pos])) pos++;

    if (pos < sv->count) {
        String_View result = {
            .data = sv->data,
            .count = pos,
        };
        sv_chop_left(sv, pos+1);
        return result;
    }

    String_View result = *sv;
    sv_chop_left(sv, sv->count);
    return result;
}

#define SV_FMT "%.*s" 
#define SV_Arg(s) (int)(s).count, (s).data

/*
// Example usage
int tokenize_and_print() {
    FILE *f = fopen(__FILE__, "rb");
    size_t capacity = 0x400 * 0x400;
    char *buffer = malloc(capacity);
    size_t size = fread(buffer, 1, capacity, f);
    printf("size = %zu\n", size);

    char *copy = strdup(buffer);

    String_View s = {
        .data = buffer,
        .count = size,
    };

    size_t word_count = 0;
    while (s.count > 0) {
        sv_trim_left(&s);
        if (s.count == 0) break;
        String_View word = sv_chop_by_type(&s, isspace);
        printf(SV_FMT"\n", SV_Arg(word));
        word_count++;
    }

    printf("word count = %zu\n", word_count);

    return 0;
}
*/
