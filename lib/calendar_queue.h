#ifndef DS_CALENDAR_QUEUE_H
#define DS_CALENDAR_QUEUE_H

/*
 * Calendar queue (Brown, 1988): a priority queue for discrete-event
 * simulation keyed by event time (double), with a by-value payload of
 * elem_size bytes.
 *
 * Events are hashed by time into an array of buckets ("days") of equal width;
 * one pass over the array is a "year". Dequeue looks at the current day and
 * takes its earliest event if it falls in this year, otherwise moves to the
 * next day; after a full year without a hit it searches all days directly.
 * The array doubles or halves as the queue grows or shrinks, and the day
 * width is re-estimated from the spacing of the earliest events at each
 * resize; the estimator is a pluggable policy.
 *
 * Events with equal times are dequeued first-in first-out. Times may be
 * anything (not only after the last dequeued time); an event earlier than
 * the current day moves the calendar back to it.
 *
 * Complexity: O(1) expected enqueue and dequeue when the width matches the
 * event spacing (the usual simulation case); O(n log n) per resize, which
 * happens after Theta(n) operations. Worst case O(n) per operation.
 * Concurrency: a queue may be used by one thread at a time.
 */

#include "common.h"
#include "context.h"
#include "status.h"
#include "vec.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Width policy: given the `count` earliest event times, ascending, return
 * the new day width (> 0), or a value <= 0 to keep `current_width`. */
typedef double (*ds_calendar_queue_width_func_t)(const double *times,
                                                 size_t count,
                                                 double current_width,
                                                 void *user);

typedef struct ds_calendar_queue_config {
  size_t elem_size;
  size_t initial_buckets; /* default 2 */
  double initial_width;   /* default 1.0 */
  bool resize;            /* default true */
  size_t sample_size;     /* events sampled for the width, default 25 */
  ds_calendar_queue_width_func_t width; /* default: Brown's estimator */
  void *width_user;
  ds_context_t *context;
} ds_calendar_queue_config_t;

typedef struct ds_calendar_queue {
  ds_vec_t *buckets; /* each sorted latest-first: the earliest is at back */
  size_t bucket_count;
  double width;
  size_t current; /* bucket of the current day */
  double day;     /* number of the current day: floor(time / width) */
  size_t size;
  unsigned long seq;
  ds_calendar_queue_config_t config;
} ds_calendar_queue_t;

void ds_calendar_queue_config_init(ds_calendar_queue_config_t *config);
ds_status_t ds_calendar_queue_init(ds_calendar_queue_t *queue,
                                   const ds_calendar_queue_config_t *config);
void ds_calendar_queue_destroy(ds_calendar_queue_t *queue);
void ds_calendar_queue_clear(ds_calendar_queue_t *queue);

ds_status_t ds_calendar_queue_push(ds_calendar_queue_t *queue, double time,
                                   const void *elem);
/* DS_EMPTY when empty. Either out pointer may be NULL. */
ds_status_t ds_calendar_queue_pop(ds_calendar_queue_t *queue, double *out_time,
                                  void *out_elem);
/* The earliest event without removing it; advances the calendar. The
 * payload pointer stays valid until the next push or pop. */
ds_status_t ds_calendar_queue_peek(ds_calendar_queue_t *queue, double *out_time,
                                   void **out_elem);

size_t ds_calendar_queue_size(const ds_calendar_queue_t *queue);
bool ds_calendar_queue_is_empty(const ds_calendar_queue_t *queue);
size_t ds_calendar_queue_bucket_count(const ds_calendar_queue_t *queue);
double ds_calendar_queue_width(const ds_calendar_queue_t *queue);

/* Brown's estimator: three times the mean gap of the sampled times after
 * discarding gaps above twice the mean. */
double ds_calendar_queue_brown_width(const double *times, size_t count,
                                     double current_width, void *user);

#ifdef __cplusplus
}
#endif

#endif /* DS_CALENDAR_QUEUE_H */
