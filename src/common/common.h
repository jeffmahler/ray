#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#ifndef _WIN32
#include <execinfo.h>
#endif

#include "utarray.h"
#include "sha256.h"

/** Definitions for Ray logging levels. */
#define RAY_COMMON_DEBUG 0
#define RAY_COMMON_INFO 1
#define RAY_COMMON_WARNING 2
#define RAY_COMMON_ERROR 3
#define RAY_COMMON_FATAL 4

/**
 * RAY_COMMON_LOG_LEVEL should be defined to one of the above logging level
 * integer values. Any logging statement in the code with a logging level
 * greater than or equal to RAY_COMMON_LOG_LEVEL will be outputted to stderr.
 * The default logging level is INFO. */
#ifndef RAY_COMMON_LOG_LEVEL
#define RAY_COMMON_LOG_LEVEL RAY_COMMON_INFO
#endif

#define UNUSED(x) ((void) (x))

/**
 * Macros to enable each level of Ray logging statements depending on the
 * current logging level. */
#if (RAY_COMMON_LOG_LEVEL > RAY_COMMON_DEBUG)
#define LOG_DEBUG(M, ...)
#else
#define LOG_DEBUG(M, ...) \
  fprintf(stderr, "[DEBUG] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#if (RAY_COMMON_LOG_LEVEL > RAY_COMMON_INFO)
#define LOG_INFO(M, ...)
#else
#define LOG_INFO(M, ...) \
  fprintf(stderr, "[INFO] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#if (RAY_COMMON_LOG_LEVEL > RAY_COMMON_WARNING)
#define LOG_WARN(M, ...)
#else
#define LOG_WARN(M, ...) \
  fprintf(stderr, "[WARN] (%s:%d) " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#if (RAY_COMMON_LOG_LEVEL > RAY_COMMON_ERROR)
#define LOG_ERROR(M, ...)
#else
#define LOG_ERROR(M, ...)                                                   \
  fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, \
          errno == 0 ? "None" : strerror(errno), ##__VA_ARGS__)
#endif

#if (RAY_COMMON_LOG_LEVEL > RAY_COMMON_FATAL)
#define LOG_FATAL(M, ...)
#elif defined(_EXECINFO_H) || !defined(_WIN32)
#define LOG_FATAL(M, ...)                                                     \
  do {                                                                        \
    fprintf(stderr, "[FATAL] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, \
            errno == 0 ? "None" : strerror(errno), ##__VA_ARGS__);            \
    void *buffer[255];                                                        \
    const int calls = backtrace(buffer, sizeof(buffer) / sizeof(void *));     \
    backtrace_symbols_fd(buffer, calls, 1);                                   \
    abort();                                                                  \
  } while (0)
#else
#define LOG_FATAL(M, ...)                                                     \
  do {                                                                        \
    fprintf(stderr, "[FATAL] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, \
            errno == 0 ? "None" : strerror(errno), ##__VA_ARGS__);            \
    exit(-1);                                                                 \
  } while (0)
#endif

/** Assertion definitions, with optional logging. */
#define CHECKM(COND, M, ...)                                   \
  if (!(COND)) {                                               \
    LOG_FATAL("Check failure: %s \n" M, #COND, ##__VA_ARGS__); \
  }

#define CHECK(COND) CHECKM(COND, "")

/* This should be defined if we want to check calls to DCHECK. */
#define RAY_DCHECK

#ifdef RAY_DCHECK
#define DCHECK(COND) CHECK(COND)
#else
#define DCHECK(COND)
#endif

/* These are exit codes for common errors that can occur in Ray components. */
#define EXIT_COULD_NOT_BIND_PORT -2

/** This macro indicates that this pointer owns the data it is pointing to
 *  and is responsible for freeing it. */
#define OWNER

/** Definitions for unique ID types. */
#define UNIQUE_ID_SIZE 20

#define UNIQUE_ID_EQ(id1, id2) (memcmp((id1).id, (id2).id, UNIQUE_ID_SIZE) == 0)

#define IS_NIL_ID(id) UNIQUE_ID_EQ(id, NIL_ID)

typedef struct { unsigned char id[UNIQUE_ID_SIZE]; } unique_id;

extern const UT_icd object_id_icd;

extern const unique_id NIL_ID;

/* Generate a globally unique ID. */
unique_id globally_unique_id(void);

#define NIL_OBJECT_ID NIL_ID

typedef unique_id object_id;

#define ID_STRING_SIZE (2 * UNIQUE_ID_SIZE + 1)

/**
 * Convert an object ID to a hexdecimal string. This function assumes that
 * buffer points to an already allocated char array of size ID_STRING_SIZE. And
 * it writes a null-terminated hex-formatted string to id_string.
 *
 * @param obj_id The object ID to convert to a string.
 * @param id_string A buffer to write the string to. It is assumed that this is
 *        managed by the caller and is sufficiently long to store the object ID
 *        string.
 * @param id_length The length of the id_string buffer.
 */
char *object_id_to_string(object_id obj_id, char *id_string, int id_length);

/**
 * Compare two object IDs.
 *
 * @param first_id The first object ID to compare.
 * @param second_id The first object ID to compare.
 * @return True if the object IDs are the same and false otherwise.
 */
bool object_ids_equal(object_id first_id, object_id second_id);

/**
 * Compare a object ID to the nil ID.
 *
 * @param id The object ID to compare to nil.
 * @return True if the object ID is equal to nil.
 */
bool object_id_is_nil(object_id id);

typedef unique_id db_client_id;

/**
 * Compare two db client IDs.
 *
 * @param first_id The first db client ID to compare.
 * @param second_id The first db client ID to compare.
 * @return True if the db client IDs are the same and false otherwise.
 */
bool db_client_ids_equal(db_client_id first_id, db_client_id second_id);

#define MAX(x, y) ((x) >= (y) ? (x) : (y))
#define MIN(x, y) ((x) <= (y) ? (x) : (y))

/** Definitions for computing hash digests. */
#define DIGEST_SIZE SHA256_BLOCK_SIZE

extern const unsigned char NIL_DIGEST[DIGEST_SIZE];

#endif
