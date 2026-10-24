// Jolt Physics on the PlayStation Vita.
//
// Jolt/Core/Core.h stops with "Unknown platform" on the Vita, but checks JPH_PLATFORM_BLUE first and then
// includes this file, which it expects the platform to provide (Jolt itself ships none: the console it was
// written for is under NDA). cmake/Vita.cmake defines JPH_PLATFORM_BLUE on the Jolt target and puts this
// directory on its include path, both PUBLIC, so the engine's Physics*.cpp see the same configuration and
// mb3-deps/jolt stays unpatched.
//
// What Core.h asks for (its comment at the include, plus Mutex.h, Semaphore.h/.cpp and TickCounter.h):
// JPH_BREAKPOINT, JPH_PLATFORM_BLUE_GET_TICKS, and the MUTEX, RWLOCK and SEMAPHORE macros. They are mapped
// to the C++ standard library over newlib's pthreads. Jolt's own semaphore keeps an atomic count and only
// touches this one when a thread has to wait, so a mutex and a condition variable are enough.

#pragma once

#include <psp2/kernel/processmgr.h>

#include <alloca.h>  // JPH_STACK_ALLOC is alloca; Core.h includes its header per platform, here it is ours to include
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

// Jolt's aligned allocator calls posix_memalign (Memory.cpp). Newlib declares it only outside strict ISO
// mode, which Jolt's -std=c++17 is, and does not implement it at all; platform/vita/src/posix_memalign.c
// does (linked through mb3_vita_compat), and this is its declaration.
extern "C" int posix_memalign(void** memptr, size_t alignment, size_t size);

#define JPH_BREAKPOINT __builtin_trap()

// Microseconds since the process started; Jolt only compares differences (the profiler is off anyway).
#define JPH_PLATFORM_BLUE_GET_TICKS() static_cast<std::uint64_t>(sceKernelGetProcessTimeWide())

#define JPH_PLATFORM_BLUE_MUTEX std::mutex
#define JPH_PLATFORM_BLUE_MUTEX_INIT(m) ((void)0)
#define JPH_PLATFORM_BLUE_MUTEX_DESTROY(m) ((void)0)
#define JPH_PLATFORM_BLUE_MUTEX_TRYLOCK(m) (m).try_lock()
#define JPH_PLATFORM_BLUE_MUTEX_LOCK(m) (m).lock()
#define JPH_PLATFORM_BLUE_MUTEX_UNLOCK(m) (m).unlock()

#define JPH_PLATFORM_BLUE_RWLOCK std::shared_mutex
#define JPH_PLATFORM_BLUE_RWLOCK_INIT(l) ((void)0)
#define JPH_PLATFORM_BLUE_RWLOCK_DESTROY(l) ((void)0)
#define JPH_PLATFORM_BLUE_RWLOCK_TRYWLOCK(l) (l).try_lock()
#define JPH_PLATFORM_BLUE_RWLOCK_TRYRLOCK(l) (l).try_lock_shared()
#define JPH_PLATFORM_BLUE_RWLOCK_WLOCK(l) (l).lock()
#define JPH_PLATFORM_BLUE_RWLOCK_WUNLOCK(l) (l).unlock()
#define JPH_PLATFORM_BLUE_RWLOCK_RLOCK(l) (l).lock_shared()
#define JPH_PLATFORM_BLUE_RWLOCK_RUNLOCK(l) (l).unlock_shared()

namespace mb3_vita {

// A counting semaphore: Signal adds units, Wait takes them, sleeping until enough are there. Jolt calls
// Wait with the number of units it needs at once (Semaphore::Acquire), which is what the loop below gives.
struct Semaphore {
    std::mutex mutex;
    std::condition_variable available;
    int count = 0;

    void signal(int units) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            count += units;
        }
        available.notify_all();
    }

    void wait(int units) {
        std::unique_lock<std::mutex> lock(mutex);
        available.wait(lock, [&] { return count >= units; });
        count -= units;
    }
};

}  // namespace mb3_vita

#define JPH_PLATFORM_BLUE_SEMAPHORE mb3_vita::Semaphore
#define JPH_PLATFORM_BLUE_SEMAPHORE_INIT(s) true
#define JPH_PLATFORM_BLUE_SEMAPHORE_DESTROY(s) ((void)0)
#define JPH_PLATFORM_BLUE_SEMAPHORE_SIGNAL(s, n) (s).signal(static_cast<int>(n))
#define JPH_PLATFORM_BLUE_SEMAPHORE_WAIT(s, n) (s).wait(static_cast<int>(n))
