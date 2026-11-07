#include "calendar_queue.h"
#include <math.h>
#include <string.h>

/* A record is time | sequence number | payload, accessed with memcpy. */
#define CQ_HEAD (sizeof(double) + sizeof(unsigned long))

static size_t cq_rec_size(const ds_calendar_queue_t *q) {
  return CQ_HEAD + q->config.elem_size;
}

static double cq_time(const unsigned char *rec) {
  double t;
  memcpy(&t, rec, sizeof t);
  return t;
}

static unsigned long cq_seq(const unsigned char *rec) {
  unsigned long s;
  memcpy(&s, rec + sizeof(double), sizeof s);
  return s;
}

/* Ascending (time, seq). */
static int cq_rec_cmp(const void *a, const void *b, void *user) {
  double ta = cq_time((const unsigned char *)a);
  double tb = cq_time((const unsigned char *)b);
  unsigned long sa;
  unsigned long sb;
  (void)user;
  if (ta < tb) {
    return -1;
  }
  if (ta > tb) {
    return 1;
  }
  sa = cq_seq((const unsigned char *)a);
  sb = cq_seq((const unsigned char *)b);
  return (sa > sb) - (sa < sb);
}

static double cq_day_of(double width, double t) { return floor(t / width); }

static size_t cq_bucket_of_day(size_t count, double day) {
  double m = fmod(day, (double)count);
  if (m < 0) {
    m += (double)count;
  }
  if (!(m >= 0 && m < (double)count)) {
    m = 0; /* day out of double range: any fixed bucket stays correct */
  }
  return (size_t)m;
}

static void cq_seek(ds_calendar_queue_t *q, double t) {
  q->day = cq_day_of(q->width, t);
  q->current = cq_bucket_of_day(q->bucket_count, q->day);
}

double ds_calendar_queue_brown_width(const double *times, size_t count,
                                     double current_width, void *user) {
  double sum = 0;
  double mean;
  double kept = 0;
  size_t kept_count = 0;
  size_t i;
  double gap;
  (void)user;
  if (count < 2) {
    return current_width;
  }
  for (i = 1; i < count; i++) {
    sum += times[i] - times[i - 1];
  }
  mean = sum / (double)(count - 1);
  for (i = 1; i < count; i++) {
    gap = times[i] - times[i - 1];
    if (gap <= 2 * mean) {
      kept += gap;
      kept_count++;
    }
  }
  if (kept_count == 0 || !(kept > 0)) {
    return current_width;
  }
  return 3 * kept / (double)kept_count;
}

void ds_calendar_queue_config_init(ds_calendar_queue_config_t *config) {
  config->elem_size = 0;
  config->initial_buckets = 2;
  config->initial_width = 1.0;
  config->resize = true;
  config->sample_size = 25;
  config->width = ds_calendar_queue_brown_width;
  config->width_user = NULL;
  config->context = NULL;
}

static void cq_free_buckets(ds_context_t *ctx, ds_vec_t *buckets,
                            size_t count) {
  size_t i;
  if (buckets == NULL) {
    return;
  }
  for (i = 0; i < count; i++) {
    ds_vec_destroy(&buckets[i]);
  }
  ds_context_free(ctx, buckets);
}

static ds_vec_t *cq_new_buckets(const ds_calendar_queue_t *q, size_t count) {
  ds_vec_t *b;
  size_t i;
  if (count > ((size_t)-1) / sizeof(ds_vec_t)) {
    return NULL;
  }
  b = (ds_vec_t *)ds_context_alloc(q->config.context, count * sizeof(ds_vec_t));
  if (b == NULL) {
    return NULL;
  }
  for (i = 0; i < count; i++) {
    if (ds_vec_init(&b[i], cq_rec_size(q), q->config.context) != DS_OK) {
      cq_free_buckets(q->config.context, b, i);
      return NULL;
    }
  }
  return b;
}

ds_status_t ds_calendar_queue_init(ds_calendar_queue_t *queue,
                                   const ds_calendar_queue_config_t *config) {
  if (queue == NULL) {
    return DS_ERR_NULL;
  }
  if (config != NULL) {
    queue->config = *config;
  } else {
    ds_calendar_queue_config_init(&queue->config);
  }
  if (queue->config.initial_buckets == 0) {
    queue->config.initial_buckets = 2;
  }
  if (!(queue->config.initial_width > 0)) {
    return DS_ERR_INVALID;
  }
  if (queue->config.width == NULL) {
    queue->config.width = ds_calendar_queue_brown_width;
  }
  queue->bucket_count = queue->config.initial_buckets;
  queue->width = queue->config.initial_width;
  queue->current = 0;
  queue->day = 0;
  queue->size = 0;
  queue->seq = 0;
  queue->buckets = cq_new_buckets(queue, queue->bucket_count);
  return queue->buckets == NULL ? DS_ERR_ALLOC : DS_OK;
}

void ds_calendar_queue_destroy(ds_calendar_queue_t *queue) {
  if (queue == NULL) {
    return;
  }
  cq_free_buckets(queue->config.context, queue->buckets, queue->bucket_count);
  queue->buckets = NULL;
  queue->bucket_count = 0;
  queue->size = 0;
}

void ds_calendar_queue_clear(ds_calendar_queue_t *queue) {
  size_t i;
  for (i = 0; i < queue->bucket_count; i++) {
    ds_vec_clear(&queue->buckets[i]);
  }
  queue->size = 0;
  queue->day = 0;
  queue->current = 0;
}

/* Insert into a bucket kept latest-first. */
static ds_status_t cq_bucket_insert(ds_vec_t *bucket, const void *rec) {
  size_t lo = 0;
  size_t hi = bucket->size;
  size_t mid;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (cq_rec_cmp(bucket->data + mid * bucket->elem_size, rec, NULL) > 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return ds_vec_insert(bucket, lo, rec);
}

/* Rebuild with `count` buckets and a re-estimated width. On any allocation
 * failure the queue is left as it was. */
static void cq_resize(ds_calendar_queue_t *q, size_t count) {
  ds_context_t *ctx = q->config.context;
  ds_vec_t all;
  ds_vec_t *nb;
  double *times = NULL;
  size_t i;
  size_t k;
  size_t rs = cq_rec_size(q);
  double width = q->width;
  double w;
  const unsigned char *rec;

  if (ds_vec_init(&all, rs, ctx) != DS_OK) {
    return;
  }
  if (ds_vec_reserve(&all, q->size) != DS_OK) {
    ds_vec_destroy(&all);
    return;
  }
  for (i = 0; i < q->bucket_count; i++) {
    (void)ds_vec_append(&all, q->buckets[i].data, q->buckets[i].size);
  }
  ds_vec_sort(&all, cq_rec_cmp, NULL);

  k = all.size < q->config.sample_size ? all.size : q->config.sample_size;
  if (k >= 2) {
    times = (double *)ds_context_alloc(ctx, k * sizeof(double));
    if (times == NULL) {
      ds_vec_destroy(&all);
      return;
    }
    for (i = 0; i < k; i++) {
      times[i] = cq_time(all.data + i * rs);
    }
    w = q->config.width(times, k, width, q->config.width_user);
    if (w > 0) {
      width = w;
    }
    ds_context_free(ctx, times);
  }

  nb = cq_new_buckets(q, count);
  if (nb == NULL) {
    ds_vec_destroy(&all);
    return;
  }
  /* Latest first, so each bucket ends with its earliest record. */
  for (i = all.size; i-- > 0;) {
    rec = all.data + i * rs;
    if (ds_vec_push(
            &nb[cq_bucket_of_day(count, cq_day_of(width, cq_time(rec)))],
            rec) != DS_OK) {
      cq_free_buckets(ctx, nb, count);
      ds_vec_destroy(&all);
      return;
    }
  }
  cq_free_buckets(ctx, q->buckets, q->bucket_count);
  q->buckets = nb;
  q->bucket_count = count;
  q->width = width;
  if (all.size > 0) {
    cq_seek(q, cq_time(all.data));
  }
  ds_vec_destroy(&all);
}

ds_status_t ds_calendar_queue_push(ds_calendar_queue_t *queue, double time,
                                   const void *elem) {
  unsigned char stackrec[64];
  unsigned char *rec = stackrec;
  size_t rs = cq_rec_size(queue);
  ds_status_t status;
  double day;
  if (elem == NULL && queue->config.elem_size > 0) {
    return DS_ERR_NULL;
  }
  if (!(time > -HUGE_VAL && time < HUGE_VAL)) { /* NaN or infinite */
    return DS_ERR_INVALID;
  }
  if (rs > sizeof stackrec) {
    rec = (unsigned char *)ds_context_alloc(queue->config.context, rs);
    if (rec == NULL) {
      return DS_ERR_ALLOC;
    }
  }
  memcpy(rec, &time, sizeof time);
  memcpy(rec + sizeof(double), &queue->seq, sizeof queue->seq);
  if (queue->config.elem_size > 0) {
    memcpy(rec + CQ_HEAD, elem, queue->config.elem_size);
  }
  day = cq_day_of(queue->width, time);
  status = cq_bucket_insert(
      &queue->buckets[cq_bucket_of_day(queue->bucket_count, day)], rec);
  if (rec != stackrec) {
    ds_context_free(queue->config.context, rec);
  }
  if (status != DS_OK) {
    return status;
  }
  queue->seq++;
  if (queue->size == 0 || day < queue->day) {
    cq_seek(queue, time);
  }
  queue->size++;
  if (queue->config.resize && queue->size > 2 * queue->bucket_count) {
    cq_resize(queue, 2 * queue->bucket_count);
  }
  return DS_OK;
}

/* Locate the earliest record: leaves queue->current on its bucket. */
static unsigned char *cq_find(ds_calendar_queue_t *q) {
  size_t k;
  size_t i;
  ds_vec_t *b;
  unsigned char *back;
  unsigned char *best = NULL;
  for (k = 0; k < q->bucket_count; k++) {
    b = &q->buckets[q->current];
    if (b->size > 0) {
      back = b->data + (b->size - 1) * b->elem_size;
      if (cq_day_of(q->width, cq_time(back)) <= q->day) {
        return back;
      }
    }
    q->current = q->current + 1 == q->bucket_count ? 0 : q->current + 1;
    q->day += 1;
  }
  /* A whole year without a hit: search every day directly. */
  for (i = 0; i < q->bucket_count; i++) {
    b = &q->buckets[i];
    if (b->size > 0) {
      back = b->data + (b->size - 1) * b->elem_size;
      if (best == NULL || cq_rec_cmp(back, best, NULL) < 0) {
        best = back;
      }
    }
  }
  cq_seek(q, cq_time(best));
  return best;
}

ds_status_t ds_calendar_queue_pop(ds_calendar_queue_t *queue, double *out_time,
                                  void *out_elem) {
  unsigned char *rec;
  if (queue->size == 0) {
    return DS_EMPTY;
  }
  rec = cq_find(queue);
  if (out_time != NULL) {
    *out_time = cq_time(rec);
  }
  if (out_elem != NULL && queue->config.elem_size > 0) {
    memcpy(out_elem, rec + CQ_HEAD, queue->config.elem_size);
  }
  queue->buckets[queue->current].size--;
  queue->size--;
  if (queue->config.resize && queue->bucket_count > 2 &&
      queue->size < queue->bucket_count / 2) {
    cq_resize(queue, queue->bucket_count / 2);
  }
  return DS_OK;
}

ds_status_t ds_calendar_queue_peek(ds_calendar_queue_t *queue, double *out_time,
                                   void **out_elem) {
  unsigned char *rec;
  if (queue->size == 0) {
    return DS_EMPTY;
  }
  rec = cq_find(queue);
  if (out_time != NULL) {
    *out_time = cq_time(rec);
  }
  if (out_elem != NULL) {
    *out_elem = queue->config.elem_size > 0 ? rec + CQ_HEAD : NULL;
  }
  return DS_OK;
}

size_t ds_calendar_queue_size(const ds_calendar_queue_t *queue) {
  return queue->size;
}

bool ds_calendar_queue_is_empty(const ds_calendar_queue_t *queue) {
  return queue->size == 0;
}

size_t ds_calendar_queue_bucket_count(const ds_calendar_queue_t *queue) {
  return queue->bucket_count;
}

double ds_calendar_queue_width(const ds_calendar_queue_t *queue) {
  return queue->width;
}
