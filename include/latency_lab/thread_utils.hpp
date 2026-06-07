#pragma once

namespace latency_lab {

bool pin_thread_to_cpu(int cpu_id) noexcept;
int get_current_cpu() noexcept;

}
