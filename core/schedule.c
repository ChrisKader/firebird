#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "emu.h"
#include "schedule.h"

sched_state sched;

/* Track which sched item is currently being processed to avoid
 * re-entrant event_clear recursion. */
static int sched_current_index = -1;

static inline uint32_t muldiv(uint32_t a, uint32_t b, uint32_t c) {
#if defined(__i386__) || defined(__x86_64__)
    __asm__ ("mull %1; divl %2" : "+a" (a) : "m" (b), "m" (c) : "edx");
    return a;
#else
    uint64_t d = a;
    d *= b;
    return d / c;
#endif
}

void sched_reset(void) {
    const uint32_t def_rates[] = { 0, 0, 0, 27000000, 12000000, 32768 };
    memcpy(sched.clock_rates, def_rates, sizeof(def_rates));
    memset(sched.items, 0, sizeof sched.items);
    sched.next_cputick = 0;
    sched.next_index = -1;
}

void event_repeat(int index, uint32_t ticks) {
    struct sched_item *item = &sched.items[index];

    uint32_t prev = item->tick;
    item->second = ticks / sched.clock_rates[item->clock];
    item->tick = ticks % sched.clock_rates[item->clock];
    if (prev >= sched.clock_rates[item->clock] - item->tick) {
        item->second++;
        item->tick -= sched.clock_rates[item->clock];
    }
    item->tick += prev;

    item->cputick = muldiv(item->tick, sched.clock_rates[CLOCK_CPU], sched.clock_rates[item->clock]);
}

void sched_update_next_event(uint32_t cputick) {
    sched.next_cputick = sched.clock_rates[CLOCK_CPU];
    sched.next_index = -1;
    for (int i = 0; i < SCHED_NUM_ITEMS; i++) {
        struct sched_item *item = &sched.items[i];
        if (item->proc != NULL && item->second == 0 && item->cputick < sched.next_cputick) {
            sched.next_cputick = item->cputick;
            sched.next_index = i;
        }
    }
    //printf("Next event: (%8d,%d)\n", next_cputick, next_index);
    cycle_count_delta = cputick - sched.next_cputick;
}

uint32_t sched_process_pending_events() {
    uint32_t cputick = sched.next_cputick + cycle_count_delta;
    while (cputick >= sched.next_cputick) {
        if (sched.next_index < 0) {
            //printf("[%8d] New second\n", cputick);
            int i;
            for (i = 0; i < SCHED_NUM_ITEMS; i++) {
                if (sched.items[i].second >= 0)
                    sched.items[i].second--;
            }
            cputick -= sched.clock_rates[CLOCK_CPU];
        } else {
            //printf("[%8d/%8d] Event %d\n", cputick, next_cputick, next_index);
            sched.items[sched.next_index].second = -1;
            sched_current_index = sched.next_index;
            sched.items[sched.next_index].proc(sched.next_index);
            sched_current_index = -1;
        }
        sched_update_next_event(cputick);
    }
    return cputick;
}

void event_clear(int index) {
    /* If we are currently inside this event handler, just mark it
     * inactive; running sched_process_pending_events recursively
     * would blow the stack. */
    if (index == sched_current_index) {
        sched.items[index].second = -1;
        sched.items[index].tick = 0;
        sched.items[index].cputick = 0;
        return;
    }

    uint32_t cputick = sched_process_pending_events();

    sched.items[index].second = -1;

    sched_update_next_event(cputick);
}
void event_set(int index, int ticks) {
    uint32_t cputick = sched_process_pending_events();

    struct sched_item *item = &sched.items[index];
    item->tick = muldiv(cputick, sched.clock_rates[item->clock], sched.clock_rates[CLOCK_CPU]);
    event_repeat(index, ticks);

    sched_update_next_event(cputick);
}

uint32_t event_ticks_remaining(int index) {
    uint32_t cputick = sched_process_pending_events();

    struct sched_item *item = &sched.items[index];
    return item->second * sched.clock_rates[item->clock]
            + item->tick - muldiv(cputick, sched.clock_rates[item->clock], sched.clock_rates[CLOCK_CPU]);
}

void sched_set_clocks(int count, uint32_t *new_rates) {
    uint32_t cputick = sched_process_pending_events();

    uint32_t remaining[SCHED_NUM_ITEMS] = {0};
    uint32_t old_rates[6];
    memcpy(old_rates, sched.clock_rates, sizeof(old_rates));
    if (old_rates[CLOCK_CPU] == 0 || new_rates[CLOCK_CPU] == 0)
        return;
    int i;
    for (i = 0; i < SCHED_NUM_ITEMS; i++) {
        struct sched_item *item = &sched.items[i];
        if (item->second >= 0) {
            uint64_t elapsed = muldiv(cputick, old_rates[item->clock], old_rates[CLOCK_CPU]);
            uint64_t total = (uint64_t)item->second * old_rates[item->clock] + item->tick;
            remaining[i] = total > elapsed ? (uint32_t)(total - elapsed) : 0;
        }
    }
    cputick = muldiv(cputick, new_rates[CLOCK_CPU], old_rates[CLOCK_CPU]);
    memcpy(sched.clock_rates, new_rates, sizeof(uint32_t) * count);
    for (i = 0; i < SCHED_NUM_ITEMS; i++) {
        struct sched_item *item = &sched.items[i];
        if (item->second >= 0) {
            item->tick = muldiv(cputick, sched.clock_rates[item->clock], sched.clock_rates[CLOCK_CPU]);
            event_repeat(i, remaining[i]);
        }
    }

    sched_update_next_event(cputick);
}

bool sched_resume(const emu_snapshot *snapshot)
{
    struct sched_state new_sched;
    if(!snapshot_read(snapshot, &new_sched, sizeof(new_sched)))
        return false;

    // sched_item::proc is a function pointer.
    // Obviously, it's not possible to just save and restore that one,
    // so we use the already initialized sched_state as source
    // for the proper proc values.
    for(int i = 0; i < SCHED_NUM_ITEMS; ++i)
    {
        if(new_sched.items[i].proc && !sched.items[i].proc)
            return false; // proc was set, but we don't have it

        new_sched.items[i].proc = sched.items[i].proc;
    }

    sched = new_sched;

    return true;
}

bool sched_suspend(emu_snapshot *snapshot)
{
    return snapshot_write(snapshot, &sched, sizeof(sched));
}
