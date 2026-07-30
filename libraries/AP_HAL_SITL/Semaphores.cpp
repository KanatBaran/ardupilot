#include <AP_HAL/AP_HAL.h>

#if CONFIG_HAL_BOARD == HAL_BOARD_SITL

#include "Semaphores.h"
#include "Scheduler.h"

extern const AP_HAL::HAL& hal;

using namespace HALSITL;

// construct a semaphore
Semaphore::Semaphore()
{
#ifdef _WIN32
    // std::recursive_mutex is default-constructed
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&_lock, &attr);
#endif
}


bool Semaphore::give()
{
    take_count--;
    if (take_count == 0) {
#ifdef _WIN32
        owner = std::thread::id{};
#else
        owner = (pthread_t)-1;
#endif
    }
#ifdef _WIN32
    try {
        _lock.unlock();
    } catch (...) {
        AP_HAL::panic("Bad semaphore usage");
    }
#else
    if (pthread_mutex_unlock(&_lock) != 0) {
        AP_HAL::panic("Bad semaphore usage");
    }
#endif
    return true;
}

void Semaphore::check_owner() const
{
    // should probably make sure we're holding the semaphore here....
#ifdef _WIN32
    if (owner != std::this_thread::get_id()) {
#else
    if (owner != pthread_self()) {
#endif
        AP_HAL::panic("Wrong owner");
    }
}

bool Semaphore::take(uint32_t timeout_ms)
{
    if (timeout_ms == HAL_SEMAPHORE_BLOCK_FOREVER) {
#ifdef _WIN32
        _lock.lock();
        owner = std::this_thread::get_id();
        take_count++;
        return true;
#else
        if (pthread_mutex_lock(&_lock) == 0) {
            owner = pthread_self();
            take_count++;
            return true;
        }
        return false;
#endif
    }
    if (take_nonblocking()) {
#ifdef _WIN32
        owner = std::this_thread::get_id();
#else
        owner = pthread_self();
#endif
        return true;
    }
    uint64_t start = AP_HAL::micros64();
    do {
        Scheduler::from(hal.scheduler)->set_in_semaphore_take_wait(true);
        hal.scheduler->delay_microseconds(200);
        Scheduler::from(hal.scheduler)->set_in_semaphore_take_wait(false);
        if (take_nonblocking()) {
#ifdef _WIN32
            owner = std::this_thread::get_id();
#else
            owner = pthread_self();
#endif
            return true;
        }
    } while ((AP_HAL::micros64() - start) < timeout_ms * 1000);
    return false;
}

bool Semaphore::take_nonblocking()
{
#ifdef _WIN32
    if (_lock.try_lock()) {
        owner = std::this_thread::get_id();
        take_count++;
        return true;
    }
#else
    if (pthread_mutex_trylock(&_lock) == 0) {
        owner = pthread_self();
        take_count++;
        return true;
    }
#endif
    return false;
}


/*
  binary semaphore using condition variables
 */

BinarySemaphore::BinarySemaphore(bool initial_state) :
    AP_HAL::BinarySemaphore(initial_state)
{
#ifndef _WIN32
    pthread_cond_init(&cond, NULL);
#endif
    pending = initial_state;
}

bool BinarySemaphore::wait(uint32_t timeout_us)
{
    WITH_SEMAPHORE(mtx);
    if (!pending) {
        if (hal.scheduler->in_main_thread() ||
            Scheduler::from(hal.scheduler)->semaphore_wait_hack_required()) {
            /*
              when in the main thread we need to do a busy wait to ensure
              the clock advances
            */
            uint64_t end_us = AP_HAL::micros64() + timeout_us;
#ifdef _WIN32
            do {
                // Pass mtx._lock directly: WITH_SEMAPHORE already owns it.
                // wait_for unlocks once while waiting and reacquires before return.
                if (cond.wait_for(mtx._lock, std::chrono::microseconds(0)) == std::cv_status::no_timeout) {
                    pending = false;
                    return true;
                }
                hal.scheduler->delay_microseconds(10);
            } while (AP_HAL::micros64() < end_us);
#else
            struct timespec ts {};
            do {
                if (pthread_cond_timedwait(&cond, &mtx._lock, &ts) == 0) {
                    pending = false;
                    return true;
                }
                hal.scheduler->delay_microseconds(10);
            } while (AP_HAL::micros64() < end_us);
#endif
            return false;
        }

#ifdef _WIN32
        // Pass mtx._lock directly so wait_for does not take an extra lock level
        // on top of WITH_SEMAPHORE's ownership.
        if (cond.wait_for(mtx._lock, std::chrono::microseconds(timeout_us)) != std::cv_status::no_timeout) {
            return false;
        }
#else
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            return false;
        }
        ts.tv_sec += timeout_us/1000000UL;
        ts.tv_nsec += (timeout_us % 1000000U) * 1000UL;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        if (pthread_cond_timedwait(&cond, &mtx._lock, &ts) != 0) {
            return false;
        }
#endif
    }
    pending = false;
    return true;
}

bool BinarySemaphore::wait_blocking(void)
{
    WITH_SEMAPHORE(mtx);
    if (!pending) {
#ifdef _WIN32
        // Pass mtx._lock directly: WITH_SEMAPHORE already owns the recursive mutex.
        cond.wait(mtx._lock);
#else
        if (pthread_cond_wait(&cond, &mtx._lock) != 0) {
            return false;
        }
#endif
    }
    pending = false;
    return true;
}

void BinarySemaphore::signal(void)
{
    WITH_SEMAPHORE(mtx);
    if (!pending) {
        pending = true;
#ifdef _WIN32
        cond.notify_one();
#else
        pthread_cond_signal(&cond);
#endif
    }
}

#endif  // CONFIG_HAL_BOARD
