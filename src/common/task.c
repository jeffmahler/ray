#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sha256.h"
#include "utarray.h"

#include "task.h"
#include "common.h"
#include "io.h"

/* TASK SPECIFICATIONS */

/* Tasks are stored in a consecutive chunk of memory, the first
 * sizeof(task_spec) bytes are arranged according to the struct
 * task_spec. Then there is an array of task_args of length
 * (num_args + num_returns), and then follows the data of
 * pass-by-value arguments of size args_value_size. The offsets in the
 * task_arg.val are with respect to the end of the augmented structure,
 * i.e. with respect to the address &task_spec.args_and_returns[0] +
 * (task_spec->num_args + task_spec->num_returns) * sizeof(task_arg). */

typedef struct {
  /* Either ARG_BY_REF or ARG_BY_VAL. */
  int8_t type;
  union {
    object_id obj_id;
    struct {
      /* Offset where the data associated to this arg is located relative
       * to &task_spec.args_and_returns[0]. */
      ptrdiff_t offset;
      int64_t length;
    } value;
  };
} task_arg;

struct task_spec_impl {
  /** ID of the driver that created this task. */
  unique_id driver_id;
  /** Task ID of the task. */
  task_id task_id;
  /** Task ID of the parent task. */
  task_id parent_task_id;
  /** A count of the number of tasks submitted by the parent task before this
   *  one. */
  int64_t parent_counter;
  /** Actor ID of the task. This is the actor that this task is executed on
   *  or NIL_ACTOR_ID if the task is just a normal task. */
  actor_id actor_id;
  /** Number of tasks that have been submitted to this actor so far. */
  int64_t actor_counter;
  /** Function ID of the task. */
  function_id function_id;
  /** Total number of arguments. */
  int64_t num_args;
  /** Index of the last argument that has been constructed. */
  int64_t arg_index;
  /** Number of return values. */
  int64_t num_returns;
  /** Number of bytes the pass-by-value arguments are occupying. */
  int64_t args_value_size;
  /** The offset of the number of bytes of pass-by-value data that
   *  has been written so far, relative to &task_spec->args_and_returns[0] +
   *  (task_spec->num_args + task_spec->num_returns) * sizeof(task_arg) */
  int64_t args_value_offset;
  /** Resource vector for this task. A resource vector maps a resource index
   *  (like "cpu" or "gpu") to the number of units of that resource required.
   *  Note that this will allow us to support arbitrary attributes:
   *  For example, we can have a coloring of nodes and "red" can correspond
   *  to 0.0, "green" to 1.0 and "yellow" to 2.0. */
  double required_resources[MAX_RESOURCE_INDEX];
  /** Argument and return IDs as well as offsets for pass-by-value args. */
  task_arg args_and_returns[0];
};

/* The size of a task specification is given by the following expression. */
#define TASK_SPEC_SIZE(NUM_ARGS, NUM_RETURNS, ARGS_VALUE_SIZE)           \
  (sizeof(task_spec) + ((NUM_ARGS) + (NUM_RETURNS)) * sizeof(task_arg) + \
   (ARGS_VALUE_SIZE))

bool task_ids_equal(task_id first_id, task_id second_id) {
  return UNIQUE_ID_EQ(first_id, second_id);
}

bool task_id_is_nil(task_id id) {
  return task_ids_equal(id, NIL_TASK_ID);
}

bool actor_ids_equal(actor_id first_id, actor_id second_id) {
  return UNIQUE_ID_EQ(first_id, second_id);
}

bool function_ids_equal(function_id first_id, function_id second_id) {
  return UNIQUE_ID_EQ(first_id, second_id);
}

bool function_id_is_nil(function_id id) {
  return function_ids_equal(id, NIL_FUNCTION_ID);
}

task_id *task_return_ptr(task_spec *spec, int64_t return_index) {
  DCHECK(0 <= return_index && return_index < spec->num_returns);
  task_arg *ret = &spec->args_and_returns[spec->num_args + return_index];
  DCHECK(ret->type == ARG_BY_REF);
  return &ret->obj_id;
}

/* Compute the task ID. This assumes that all of the other fields have been set
 * and that the return IDs have not been set. It assumes the task_spec was
 * zero-initialized so that uninitialized fields will not make the task ID
 * nondeterministic. */
task_id compute_task_id(task_spec *spec) {
  /* Check that the task ID and return ID fields of the task_spec are
   * uninitialized. */
  DCHECK(task_ids_equal(spec->task_id, NIL_TASK_ID));
  for (int i = 0; i < spec->num_returns; ++i) {
    DCHECK(object_ids_equal(*task_return_ptr(spec, i), NIL_ID));
  }
  /* Compute a SHA256 hash of the task_spec. */
  SHA256_CTX ctx;
  BYTE buff[DIGEST_SIZE];
  sha256_init(&ctx);
  sha256_update(&ctx, (BYTE *) spec, task_spec_size(spec));
  sha256_final(&ctx, buff);
  /* Create a task ID out of the hash. This will truncate the hash. */
  task_id task_id;
  CHECK(sizeof(task_id) <= DIGEST_SIZE);
  memcpy(&task_id.id, buff, sizeof(task_id.id));
  return task_id;
}

object_id task_compute_return_id(task_id task_id, int64_t return_index) {
  /* Here, return_indices need to be >= 0, so we can use negative
   * indices for put. */
  DCHECK(return_index >= 0);
  /* TODO(rkn): This line requires object and task IDs to be the same size. */
  object_id return_id = task_id;
  int64_t *first_bytes = (int64_t *) &return_id;
  /* XOR the first bytes of the object ID with the return index. We add one so
   * the first return ID is not the same as the task ID. */
  *first_bytes = *first_bytes ^ (return_index + 1);
  return return_id;
}

object_id task_compute_put_id(task_id task_id, int64_t put_index) {
  DCHECK(put_index >= 0);
  /* TODO(pcm): This line requires object and task IDs to be the same size. */
  object_id put_id = task_id;
  int64_t *first_bytes = (int64_t *) &put_id;
  /* XOR the first bytes of the object ID with the return index. We add one so
   * the first return ID is not the same as the task ID. */
  *first_bytes = *first_bytes ^ (-put_index - 1);
  return put_id;
}

task_spec *start_construct_task_spec(unique_id driver_id,
                                     task_id parent_task_id,
                                     int64_t parent_counter,
                                     actor_id actor_id,
                                     int64_t actor_counter,
                                     function_id function_id,
                                     int64_t num_args,
                                     int64_t num_returns,
                                     int64_t args_value_size) {
  int64_t size = TASK_SPEC_SIZE(num_args, num_returns, args_value_size);
  task_spec *task = malloc(size);
  memset(task, 0, size);
  task->driver_id = driver_id;
  task->task_id = NIL_TASK_ID;
  task->parent_task_id = parent_task_id;
  task->parent_counter = parent_counter;
  task->actor_id = actor_id;
  task->actor_counter = actor_counter;
  task->function_id = function_id;
  task->num_args = num_args;
  task->arg_index = 0;
  task->num_returns = num_returns;
  task->args_value_size = args_value_size;
  for (int i = 0; i < num_returns; ++i) {
    *task_return_ptr(task, i) = NIL_ID;
  }
  return task;
}

void finish_construct_task_spec(task_spec *spec) {
  /* Check that all of the arguments were added to the task. */
  DCHECK(spec->arg_index == spec->num_args);
  spec->task_id = compute_task_id(spec);
  /* Set the object IDs for the return values. */
  for (int64_t i = 0; i < spec->num_returns; ++i) {
    *task_return_ptr(spec, i) = task_compute_return_id(spec->task_id, i);
  }
}

int64_t task_spec_size(task_spec *spec) {
  return TASK_SPEC_SIZE(spec->num_args, spec->num_returns,
                        spec->args_value_size);
}

function_id task_function(task_spec *spec) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  return spec->function_id;
}

actor_id task_spec_actor_id(task_spec *spec) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  return spec->actor_id;
}

int64_t task_spec_actor_counter(task_spec *spec) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  return spec->actor_counter;
}

unique_id task_spec_driver_id(task_spec *spec) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  return spec->driver_id;
}

task_id task_spec_id(task_spec *spec) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  return spec->task_id;
}

int64_t task_num_args(task_spec *spec) {
  return spec->num_args;
}

int64_t task_num_returns(task_spec *spec) {
  return spec->num_returns;
}

int8_t task_arg_type(task_spec *spec, int64_t arg_index) {
  DCHECK(0 <= arg_index && arg_index < spec->num_args);
  return spec->args_and_returns[arg_index].type;
}

object_id task_arg_id(task_spec *spec, int64_t arg_index) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  DCHECK(0 <= arg_index && arg_index < spec->num_args);
  task_arg *arg = &spec->args_and_returns[arg_index];
  DCHECK(arg->type == ARG_BY_REF)
  return arg->obj_id;
}

uint8_t *task_arg_val(task_spec *spec, int64_t arg_index) {
  DCHECK(0 <= arg_index && arg_index < spec->num_args);
  task_arg *arg = &spec->args_and_returns[arg_index];
  DCHECK(arg->type == ARG_BY_VAL);
  uint8_t *data = (uint8_t *) &spec->args_and_returns[0];
  data += (spec->num_args + spec->num_returns) * sizeof(task_arg);
  return data + arg->value.offset;
}

int64_t task_arg_length(task_spec *spec, int64_t arg_index) {
  DCHECK(0 <= arg_index && arg_index < spec->num_args);
  task_arg *arg = &spec->args_and_returns[arg_index];
  DCHECK(arg->type == ARG_BY_VAL);
  return arg->value.length;
}

int64_t task_args_add_ref(task_spec *spec, object_id obj_id) {
  /* Check that the task is still under construction. */
  DCHECK(task_ids_equal(spec->task_id, NIL_TASK_ID));
  task_arg *arg = &spec->args_and_returns[spec->arg_index];
  arg->type = ARG_BY_REF;
  arg->obj_id = obj_id;
  return spec->arg_index++;
}

int64_t task_args_add_val(task_spec *spec, uint8_t *data, int64_t length) {
  /* Check that the task is still under construction. */
  DCHECK(task_ids_equal(spec->task_id, NIL_TASK_ID));
  task_arg *arg = &spec->args_and_returns[spec->arg_index];
  arg->type = ARG_BY_VAL;
  arg->value.offset = spec->args_value_offset;
  arg->value.length = length;
  uint8_t *addr = task_arg_val(spec, spec->arg_index);
  DCHECK(spec->args_value_offset + length <= spec->args_value_size);
  DCHECK(spec->arg_index != spec->num_args - 1 ||
         spec->args_value_offset + length == spec->args_value_size);
  memcpy(addr, data, length);
  spec->args_value_offset += length;
  return spec->arg_index++;
}

void task_spec_set_required_resource(task_spec *spec,
                                     int64_t resource_index,
                                     double value) {
  spec->required_resources[resource_index] = value;
}

object_id task_return(task_spec *spec, int64_t return_index) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  DCHECK(0 <= return_index && return_index < spec->num_returns);
  task_arg *ret = &spec->args_and_returns[spec->num_args + return_index];
  DCHECK(ret->type == ARG_BY_REF);
  return ret->obj_id;
}

double task_spec_get_required_resource(const task_spec *spec,
                                       int64_t resource_index) {
  return spec->required_resources[resource_index];
}

void free_task_spec(task_spec *spec) {
  /* Check that the task has been constructed. */
  DCHECK(!task_ids_equal(spec->task_id, NIL_TASK_ID));
  DCHECK(spec->arg_index == spec->num_args);
  free(spec);
}

void print_task(task_spec *spec, UT_string *output) {
  /* For converting an id to hex, which has double the number
   * of bytes compared to the id (+ 1 byte for '\0'). */
  static char hex[ID_STRING_SIZE];
  /* Print function id. */
  object_id_to_string((object_id) task_function(spec), &hex[0], ID_STRING_SIZE);
  utstring_printf(output, "fun %s ", &hex[0]);
  /* Print arguments. */
  for (int i = 0; i < task_num_args(spec); ++i) {
    object_id_to_string((object_id) task_arg_id(spec, i), &hex[0],
                        ID_STRING_SIZE);
    utstring_printf(output, " id:%d %s", i, &hex[0]);
  }
  /* Print return ids. */
  for (int i = 0; i < task_num_returns(spec); ++i) {
    object_id obj_id = task_return(spec, i);
    object_id_to_string(obj_id, &hex[0], ID_STRING_SIZE);
    utstring_printf(output, " ret:%d %s", i, &hex[0]);
  }
}

/* TASK INSTANCES */

struct task_impl {
  /** The scheduling state of the task. */
  int state;
  /** The ID of the local scheduler involved. */
  db_client_id local_scheduler_id;
  /** The task specification for this task. */
  task_spec spec;
};

task *alloc_task(task_spec *spec, int state, db_client_id local_scheduler_id) {
  int64_t size = sizeof(task) - sizeof(task_spec) + task_spec_size(spec);
  task *result = malloc(size);
  memset(result, 0, size);
  result->state = state;
  result->local_scheduler_id = local_scheduler_id;
  memcpy(&result->spec, spec, task_spec_size(spec));
  return result;
}

task *copy_task(task *other) {
  int64_t size = task_size(other);
  task *copy = malloc(size);
  CHECK(copy != NULL);
  memcpy(copy, other, size);
  return copy;
}

int64_t task_size(task *task_arg) {
  return sizeof(task) - sizeof(task_spec) + task_spec_size(&task_arg->spec);
}

int task_state(task *task) {
  return task->state;
}

void task_set_state(task *task, int state) {
  task->state = state;
}

db_client_id task_local_scheduler(task *task) {
  return task->local_scheduler_id;
}

void task_set_local_scheduler(task *task, db_client_id local_scheduler_id) {
  task->local_scheduler_id = local_scheduler_id;
}

task_spec *task_task_spec(task *task) {
  return &task->spec;
}

task_id task_task_id(task *task) {
  task_spec *spec = task_task_spec(task);
  return task_spec_id(spec);
}

void free_task(task *task) {
  free(task);
}
