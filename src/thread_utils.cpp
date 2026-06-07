#include "latency_lab/thread_utils.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace latency_lab {

bool pin_thread_to_cpu(int cpu_id) noexcept {
#if defined(__linux__)
    if (cpu_id < 0) {
        return false;
    }

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu_id, &cpu_set);

    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) == 0;
#else
    (void)cpu_id;
    return false;
#endif
}

int get_current_cpu() noexcept {
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

}
