#ifndef DS_TIMER_WHEEL_H
#define DS_TIMER_WHEEL_H

/*
 * Hierarchical timing wheel (Varghese & Lauck; the classic Linux kernel
 * design): thousands of timeouts with O(1) start and cancel.
 *
 * Timers are intrusive: embed a ds_timer_t in your own struct and recover the
 * struct in the callback (CAST / offsetof). The wheel itself is a fixed array
 * of list heads, so it never allocates and can live in static storage, which
 * suits interrupt-driven or bare-metal schedulers.
 *
 * Time is an unsigned long tick count chosen by the caller (milliseconds,
 * jiffies, loop iterations). ds_timer_wheel_advance(wheel, n) processes n
 * ticks; processing tick t fires every timer whose expiry is t. A timer
 * started with an expiry that is already reached (expires <= now) fires on
 * the next tick processed. Tick counts may wrap around: expiries are compared
 * modulo ULONG_MAX + 1, so a timer must not be scheduled more than
 * ULONG_MAX / 2 ticks ahead.
 *
 * Layout: DS_TIMER_WHEEL_LEVELS levels of DS_TIMER_WHEEL_SLOTS slots. Level 0
 * resolves single ticks; timers further away sit in coarser levels and are
 * moved down ("cascaded") as time approaches. Timers beyond the last level's
 * range (2^24 ticks with the defaults) are parked in the last level and
 * re-cascaded until they come within range, so any delay works.
 *
 * Callbacks run with the timer already stopped. A callback may start, restart
 * or cancel any timer, including its own (periodic timers re-add themselves).
 *
 * Complexity: start, cancel O(1); advance O(ticks + timers fired + timers
 * cascaded); each timer cascades at most DS_TIMER_WHEEL_LEVELS - 1 times per
 * 2^24 ticks of delay. Advancing an empty wheel is O(1).
 *
 * Concurrency: a wheel may be used by one thread at a time; to start timers
 * from an interrupt handler, hand them to the main loop (e.g. through a ring
 * buffer).
 */

#include "common.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS_TIMER_WHEEL_BITS 6
#define DS_TIMER_WHEEL_SLOTS (1 << DS_TIMER_WHEEL_BITS)
#define DS_TIMER_WHEEL_LEVELS 4

struct ds_timer;
typedef void (*ds_timer_callback_func_t)(struct ds_timer *timer, void *user);

typedef struct ds_timer {
  struct ds_timer *next;
  struct ds_timer **pprev; /* NULL while the timer is not pending */
  unsigned long expires;
  ds_timer_callback_func_t callback;
  void *user;
} ds_timer_t;

typedef struct ds_timer_wheel {
  ds_timer_t *slots[DS_TIMER_WHEEL_LEVELS][DS_TIMER_WHEEL_SLOTS];
  unsigned long now;
  size_t pending;
} ds_timer_wheel_t;

void FUNC(ds_timer_wheel_init)(ds_timer_wheel_t *wheel, unsigned long now);
void FUNC(ds_timer_init)(ds_timer_t *timer, ds_timer_callback_func_t callback,
                         void *user);

ds_status_t FUNC(ds_timer_wheel_start)(ds_timer_wheel_t *wheel,
                                       ds_timer_t *timer,
                                       unsigned long expires);
ds_status_t FUNC(ds_timer_wheel_start_after)(ds_timer_wheel_t *wheel,
                                             ds_timer_t *timer,
                                             unsigned long delay);
bool FUNC(ds_timer_wheel_cancel)(ds_timer_wheel_t *wheel, ds_timer_t *timer);
bool FUNC(ds_timer_is_pending)(const ds_timer_t *timer);

size_t FUNC(ds_timer_wheel_advance)(ds_timer_wheel_t *wheel,
                                    unsigned long ticks);
size_t FUNC(ds_timer_wheel_advance_to)(ds_timer_wheel_t *wheel,
                                       unsigned long time);
unsigned long FUNC(ds_timer_wheel_now)(const ds_timer_wheel_t *wheel);
size_t FUNC(ds_timer_wheel_pending)(const ds_timer_wheel_t *wheel);

#ifdef __cplusplus
}
#endif

#endif /* DS_TIMER_WHEEL_H */
