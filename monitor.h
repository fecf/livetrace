#pragma once

class Monitor {
 public:
  double cpu_usage();
  double cpu_usage(uint32_t pid);

  uint64_t total_phys_mem();
  uint64_t total_virt_mem();

  uint64_t phys_mem_usage();
  uint64_t phys_mem_usage(uint32_t pid);

  uint64_t virt_mem_usage();
  uint64_t virt_mem_usage(uint32_t pid);
};
