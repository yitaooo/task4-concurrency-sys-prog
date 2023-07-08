#include "cspinlock.h"
#include <atomic>
#include <thread>

// implementation of a spinlock
struct cspinlock {
    std::atomic<bool> locked;
};

int cspin_lock(cspinlock_t *slock) {
    bool expected = false;
    while (!slock->locked.compare_exchange_strong(expected, true)) {
        expected = false;
        std::this_thread::yield();
    }
    return 0;
}

int cspin_trylock(cspinlock_t *slock) {
    bool expected = false;
    if (slock->locked.compare_exchange_strong(expected, true)) {
        return 0;
    } else {
        return -1;
    }
}

int cspin_unlock(cspinlock_t *slock) {
    slock->locked.store(false);
    return 0;
}

cspinlock_t* cspin_alloc() {
    return new cspinlock();
}

void cspin_free(cspinlock_t *slock) {
    delete slock;
}
