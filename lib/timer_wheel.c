#include "timer_wheel.h"
#include <limits.h>

#define TW_MASK ((unsigned long)(DS_TIMER_WHEEL_SLOTS - 1))
#define TW_RANGE_BITS (DS_TIMER_WHEEL_BITS * DS_TIMER_WHEEL_LEVELS)
#define TW_MAX_DELTA ((1UL << TW_RANGE_BITS) - 1UL)

static void tw_link(ds_timer_t **head, ds_timer_t *timer) {
  timer->next = *head;
  if (timer->next != NULL) {
    timer->next->pprev = &timer->next;
  }
  *head = timer;
  timer->pprev = head;
}

static void tw_unlink(ds_timer_t *timer) {
  *timer->pprev = timer->next;
  if (timer->next != NULL) {
    timer->next->pprev = timer->pprev;
  }
  timer->next = NULL;
  timer->pprev = NULL;
}

static size_t tw_slot(unsigned long time, unsigned int level) {
  return (size_t)((time >> (level * DS_TIMER_WHEEL_BITS)) & TW_MASK);
}

/* Place a timer relative to wheel->now, the next tick to be processed. */
static void tw_place(ds_timer_wheel_t *wheel, ds_timer_t *timer) {
  unsigned long delta = timer->expires - wheel->now;
  unsigned long when = timer->expires;
  unsigned int level;
  if (delta > ULONG_MAX / 2) {
    /* Already due: fire on the next tick processed. */
    tw_link(&wheel->slots[0][tw_slot(wheel->now, 0)], timer);
    return;
  }
  if (delta > TW_MAX_DELTA) {
    /* Out of range: park at the far edge and re-cascade later. */
    when = wheel->now + TW_MAX_DELTA;
    delta = TW_MAX_DELTA;
  }
  for (level = 0; level + 1 < DS_TIMER_WHEEL_LEVELS; level++) {
    if (delta < (1UL << ((level + 1) * DS_TIMER_WHEEL_BITS))) {
      break;
    }
  }
  tw_link(&wheel->slots[level][tw_slot(when, level)], timer);
}

static void tw_detach(ds_timer_t **head, ds_timer_t **list) {
  *list = *head;
  *head = NULL;
  if (*list != NULL) {
    (*list)->pprev = list;
  }
}

static size_t tw_cascade(ds_timer_wheel_t *wheel, unsigned int level) {
  size_t index = tw_slot(wheel->now, level);
  ds_timer_t *list;
  ds_timer_t *timer;
  tw_detach(&wheel->slots[level][index], &list);
  while (list != NULL) {
    timer = list;
    tw_unlink(timer);
    tw_place(wheel, timer);
  }
  return index;
}

static size_t tw_tick(ds_timer_wheel_t *wheel) {
  size_t index = tw_slot(wheel->now, 0);
  size_t fired = 0;
  unsigned int level;
  ds_timer_t *list;
  ds_timer_t *timer;
  if (index == 0) {
    for (level = 1; level < DS_TIMER_WHEEL_LEVELS; level++) {
      if (tw_cascade(wheel, level) != 0) {
        break;
      }
    }
  }
  tw_detach(&wheel->slots[0][index], &list);
  wheel->now++;
  /* Take timers one at a time from the detached list, so a callback may
   * cancel or restart any timer, including ones still in this list. */
  while (list != NULL) {
    timer = list;
    tw_unlink(timer);
    wheel->pending--;
    fired++;
    if (timer->callback != NULL) {
      timer->callback(timer, timer->user);
    }
  }
  return fired;
}

void FUNC(ds_timer_wheel_init)(ds_timer_wheel_t *wheel, unsigned long now) {
  unsigned int level;
  unsigned int slot;
  for (level = 0; level < DS_TIMER_WHEEL_LEVELS; level++) {
    for (slot = 0; slot < DS_TIMER_WHEEL_SLOTS; slot++) {
      wheel->slots[level][slot] = NULL;
    }
  }
  wheel->now = now;
  wheel->pending = 0;
}

void FUNC(ds_timer_init)(ds_timer_t *timer, ds_timer_callback_func_t callback,
                         void *user) {
  timer->next = NULL;
  timer->pprev = NULL;
  timer->expires = 0;
  timer->callback = callback;
  timer->user = user;
}

ds_status_t FUNC(ds_timer_wheel_start)(ds_timer_wheel_t *wheel,
                                       ds_timer_t *timer,
                                       unsigned long expires) {
  if (wheel == NULL || timer == NULL) {
    return DS_ERR_NULL;
  }
  if (timer->pprev != NULL) {
    tw_unlink(timer);
  } else {
    wheel->pending++;
  }
  timer->expires = expires;
  tw_place(wheel, timer);
  return DS_OK;
}

ds_status_t FUNC(ds_timer_wheel_start_after)(ds_timer_wheel_t *wheel,
                                             ds_timer_t *timer,
                                             unsigned long delay) {
  if (wheel == NULL) {
    return DS_ERR_NULL;
  }
  return FUNC(ds_timer_wheel_start)(wheel, timer, wheel->now + delay);
}

bool FUNC(ds_timer_wheel_cancel)(ds_timer_wheel_t *wheel, ds_timer_t *timer) {
  if (timer == NULL || timer->pprev == NULL) {
    return false;
  }
  tw_unlink(timer);
  wheel->pending--;
  return true;
}

bool FUNC(ds_timer_is_pending)(const ds_timer_t *timer) {
  return timer->pprev != NULL;
}

size_t FUNC(ds_timer_wheel_advance)(ds_timer_wheel_t *wheel,
                                    unsigned long ticks) {
  size_t fired = 0;
  while (ticks > 0) {
    if (wheel->pending == 0) {
      wheel->now += ticks; /* nothing can fire or cascade */
      break;
    }
    fired += tw_tick(wheel);
    ticks--;
  }
  return fired;
}

size_t FUNC(ds_timer_wheel_advance_to)(ds_timer_wheel_t *wheel,
                                       unsigned long time) {
  unsigned long ticks = time - wheel->now;
  if (ticks > ULONG_MAX / 2) {
    return 0; /* `time` is in the past */
  }
  return FUNC(ds_timer_wheel_advance)(wheel, ticks);
}

unsigned long FUNC(ds_timer_wheel_now)(const ds_timer_wheel_t *wheel) {
  return wheel->now;
}

size_t FUNC(ds_timer_wheel_pending)(const ds_timer_wheel_t *wheel) {
  return wheel->pending;
}
