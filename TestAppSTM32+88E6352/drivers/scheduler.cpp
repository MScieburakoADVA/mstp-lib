
#include "scheduler.h"
#include "clock.h"
#include "timer.h"
#include "event_queue.h"
#include "assert.h"
#include <stdio.h>
#include <string.h>

enum class callback_type { no_arg, void_ptr };

struct timer_t
{
	bool used;
	bool repeatable;
	bool canceled;
	bool pending;
	bool irql;
	uint32_t       period;
	uint64_t       next_tick_count;
	callback_type  type;
	union
	{
		void(*callback_no_arg)();
		void(*callback_void_ptr)(void*);
		void* callback;
	};
	void*          callback_arg;
	const char*    debug_name;
};

static constexpr size_t timer_count = 32;
static timer_t timers[timer_count];

static bool scheduler_initialized;
static volatile uint64_t tick_count;

static void timer_callback_irql();

// ============================================================================

void scheduler_init (TIM_TypeDef* timer)
{
	assert (event_queue_is_init());
	assert (!scheduler_initialized);
	uint32_t clock_freq = clock_get_freq(timer);
	uint32_t reload = 999;
	uint32_t prescaler = (clock_freq / (reload + 1) / 1000) - 1;
	timer_init (timer, prescaler, reload, timer_callback_irql);
	scheduler_initialized = true;
}

bool scheduler_is_init()
{
	return scheduler_initialized;
}

uint32_t scheduler_get_time_ms32()
{
	return (uint32_t) tick_count;
}

uint64_t scheduler_get_time_ms64()
{
	return tick_count;
}

void scheduler_wait (uint32_t ms)
{
	// This function is meant to be called only with interrupts enabled.
	assert ((__get_PRIMASK() & 1) == 0);

	uint64_t start = tick_count;
	while (tick_count - start < ms)
		;
}

static void call_callback (timer_t* timer)
{
	if (timer->type == callback_type::no_arg)
		timer->callback_no_arg();
	else if (timer->type == callback_type::void_ptr)
		timer->callback_void_ptr (timer->callback_arg);
	else
		assert(false);
}

static void on_timer_event (void* arg)
{
	timer_t* timer = (timer_t*) arg;

	assert (timer->used);
	assert (timer->pending);

	if (timer->canceled)
		timer->used = false;
	else
	{
		call_callback (timer);
		timer->pending = false;
		if (timer->canceled)
			timer->used = false;
	}
}

static void on_timer_timeout (timer_t* timer)
{
	if (timer->irql)
	{
		assert (timer->used);
		assert (!timer->canceled);
		assert (!timer->pending);
		timer->pending = true;
		call_callback (timer); // note that the callback might cancel the timer
		timer->pending = false;

		if (timer->canceled)
			timer->used = false;
		else if (timer->repeatable)
			timer->next_tick_count += timer->period;
	}
	else
	{
		timer->pending = event_queue_try_push (on_timer_event, timer, timer->debug_name);
		if (!timer->pending)
		{
			// We could not post the event as the event queue was full.
			// Let's retry on next tick, to give the software some time to drain the event queue.
			timer->next_tick_count++;
		}
		else if (timer->repeatable)
			timer->next_tick_count += timer->period;
	}
}

static void timer_callback_irql()
{
	tick_count++;

	for (auto timer = &timers[0]; timer < &timers[timer_count]; timer++)
	{
		if (timer->used && (timer->next_tick_count == tick_count))
		{
			if (timer->pending)
			{
				// We are here when an event generated by this timer has already been posted to the event queue,
				// and was not yet processed, and the timer has ticked again.

				// This should only happen with periodic timers, hence the following assert.
				assert (timer->repeatable);

				// We won't post another event from the same timer, to avoid filling up the queue
				// when a have a fast periodic timer and the application does a long busy wait in user mode.
				timer->next_tick_count++;
			}
			else
				on_timer_timeout(timer);
		}
	}
}

static timer_t* schedule_internal (callback_type type, void* callback, void* callback_arg, bool irql, const char* debug_name, uint32_t period_ms, bool repeatable)
{
	assert (scheduler_initialized);

	if (repeatable)
		assert (period_ms > 0);

	uint32_t primask = __get_PRIMASK();
	__disable_irq();

	timer_t* timer = &timers[0];
	while (true)
	{
		if (!timer->used)
			break;

		timer++;
		assert (timer < &timers[timer_count]);
	}

	timer->canceled     = false;
	timer->pending      = false;
	timer->irql         = irql;
	timer->repeatable   = repeatable;
	timer->period       = period_ms;
	timer->type         = type;
	timer->callback     = callback;
	timer->callback_arg = callback_arg;
	timer->next_tick_count = tick_count + period_ms;
	timer->debug_name   = debug_name;
	timer->used         = true;

	if (period_ms == 0)
		on_timer_timeout(timer);

	__set_PRIMASK(primask);

	return timer;
}

timer_t* scheduler_schedule_irql_timer (void (*callback)(void*), void* callback_arg, const char* debug_name, uint32_t period_ms, bool repeatable)
{
	//TODO:
	// Check that we're in an interrupt. IRQL timers are meant for precise timing
	// and we can't speak of precise timing in mainline (non-interrupt) code.

	return schedule_internal (callback_type::void_ptr, (void*)callback, callback_arg, true, debug_name, period_ms, repeatable);
}

timer_t* scheduler_schedule_irql_timer (void (*callback)(), const char* debug_name, uint32_t period_ms, bool repeatable)
{
	return schedule_internal (callback_type::no_arg, (void*)callback, nullptr, true, debug_name, period_ms, repeatable);
}

timer_t* scheduler_schedule_event_timer (void (*callback)(void*), void* callback_arg, const char* debug_name, uint32_t period_ms, bool repeatable)
{
	return schedule_internal (callback_type::void_ptr, (void*)callback, callback_arg, false, debug_name, period_ms, repeatable);
}

timer_t* scheduler_schedule_event_timer (void (*callback)(), const char* debug_name, uint32_t period_ms, bool repeatable)
{
	return schedule_internal (callback_type::no_arg, (void*)callback, nullptr, false, debug_name, period_ms, repeatable);
}

void scheduler_cancel_timer (timer_t* timer)
{
	assert ((timer >= &timers[0]) && (timer < &timers[timer_count]));
	assert (timer->used);

	uint32_t primask = __get_PRIMASK();
	__disable_irq(); __NOP(); __NOP();

	if (timer->pending)
		timer->canceled = true;
	else
		timer->used = false;

	__set_PRIMASK(primask);
}
