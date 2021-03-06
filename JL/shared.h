/*
 * shared.h
 *
 *  Created on: Jun 17, 2017
 *      Author: Jeff
 */

#ifndef SHARED_H_
#define SHARED_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define lambda(return_type, function_body) \
  ({ return_type __fn__ function_body __fn__; })

#define NUMARGS(...) (sizeof((int[]){0, ##__VA_ARGS__}) / sizeof(int) - 1)

#define FILE_FN(fn, op_type) file_fn(fn, op_type, __LINE__, __func__, __FILE__)
#define FILE_OP(file, operation)                                         \
  file_op(file, ({ void __fn__ operation __fn__; }), __LINE__, __func__, \
          __FILE__)
#define FILE_OP_FN(fn, op_type, operation) \
  FILE_OP(FILE_FN(fn, op_type), operation)

#define HAS_NULL(x)                                      \
  (((x & 0x000000FF) == 0) || ((x & 0x0000FF00) == 0) || \
   ((x & 0x00FF0000) == 0) || ((x & 0xFF000000) == 0))

#define FNV_32_PRIME (0x01000193)
#define FNV_1A_32_OFFSET (0x811C9DC5)

#define GET_OR(v, e, d) ((NULL == (v)) ? (d) : ((v)->e))

extern bool DBG;

typedef void (*FileHandler)(FILE *);

FILE *file_fn(const char fn[], const char op_type[], int line_num,
              const char func_name[], const char file_name[]);
void file_op(FILE *file, FileHandler operation, int line_num,
             const char func_name[], const char file_name[]);
bool ends_with(const char *str, const char *suffix);
bool starts_with(const char *str, const char *prefix);
void strcrepl(char *src, char from, char to);
char *find_str(char *haystack, size_t hashtack_len, const char *needle,
               size_t needle_len);
bool contains_char(const char str[], char c);

int count_chars(const char str[], char c);
int string_unescape(const char escaped_str[], char *buffer, size_t buffer_len);

/*
 * Function which takes a void pointer and returns an uint32_t set value for
 * for it.
 */
typedef uint32_t (*Hasher)(const void *);
typedef int32_t (*Comparator)(const void *, const void *);
typedef void (*Action)(void *);
typedef Action Deleter;

uint32_t default_hasher(const void *ptr);
int32_t default_comparator(const void *ptr1, const void *ptr2);
uint32_t string_hasher(const void *ptr);
int32_t string_comparator(const void *ptr1, const void *ptr2);

uint32_t string_hasher_len(const char *ptr, size_t len);

int getline(char **lineptr, size_t *n, FILE *stream);

void split_path_file(const char path_file[], char **path, char **file_name,
                     char **ext);
char *combine_path_file(const char path[], const char file_name[],
                        const char ext[]);

#endif /* SHARED_H_ */
