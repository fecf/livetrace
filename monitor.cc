#include "monitor.h"

#include <windows.h>
#include <psapi.h>

#include <pdh.h>
#pragma comment(lib, "pdh.lib")

#include <wil/resource.h>

double Monitor::cpu_usage() {
  PDH_HQUERY query;
  ::PdhOpenQueryW(NULL, NULL, &query);

  PDH_HCOUNTER counter;
  ::PdhAddCounterW(query, L"\\Processor(_Total)\\% Processor Time", NULL, &counter);
  ::PdhCollectQueryData(query);

  PDH_FMT_COUNTERVALUE value;
  ::PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &value);

  ::PdhCloseQuery(query);
  return value.doubleValue;
}

double Monitor::cpu_usage(uint32_t pid) {
  static ULARGE_INTEGER prev_time, prev_kernel, prev_user;

  SYSTEM_INFO si{};
  ::GetSystemInfo(&si);

  FILETIME ftime;
  ::GetSystemTimeAsFileTime(&ftime);

  wil::unique_handle handle(::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
  if (!handle) {
    return DBL_MIN;
  }

  FILETIME fcreation, fexit, fkernel, fuser;
  ::GetProcessTimes(handle.get(), &fcreation, &fexit, &fkernel, &fuser);

  ULARGE_INTEGER* kernel = (ULARGE_INTEGER*)&fkernel;
  ULARGE_INTEGER* user = (ULARGE_INTEGER*)&fuser;
  ULARGE_INTEGER* time = (ULARGE_INTEGER*)&ftime;
  double usage = (double)((kernel->QuadPart - prev_kernel.QuadPart) +
                          (user->QuadPart - prev_user.QuadPart));
  usage /= (time->QuadPart - prev_time.QuadPart);
  usage /= si.dwNumberOfProcessors;

  prev_time = *(ULARGE_INTEGER*)(&ftime);
  prev_kernel = *kernel;
  prev_user = *user;

  return usage;
}

uint64_t Monitor::total_phys_mem() {
  MEMORYSTATUSEX memInfo{.dwLength = sizeof(memInfo)};
  ::GlobalMemoryStatusEx(&memInfo);
  return memInfo.ullTotalPhys;
}

uint64_t Monitor::total_virt_mem() {
  MEMORYSTATUSEX memInfo{.dwLength = sizeof(memInfo)};
  ::GlobalMemoryStatusEx(&memInfo);
  return memInfo.ullTotalPageFile;
}

uint64_t Monitor::phys_mem_usage() {
  MEMORYSTATUSEX memInfo{.dwLength = sizeof(memInfo)};
  ::GlobalMemoryStatusEx(&memInfo);
  return memInfo.ullTotalPhys - memInfo.ullAvailPhys;
}

uint64_t Monitor::phys_mem_usage(uint32_t pid) {
  wil::unique_handle handle(::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
  if (!handle) {
    return -1;
  }
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  ::GetProcessMemoryInfo(handle.get(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
  return pmc.WorkingSetSize;
}

uint64_t Monitor::virt_mem_usage() {
  MEMORYSTATUSEX memInfo{.dwLength = sizeof(memInfo)};
  ::GlobalMemoryStatusEx(&memInfo);
  return memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;
}

uint64_t Monitor::virt_mem_usage(uint32_t pid) {
  wil::unique_handle handle(::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
  if (!handle) {
    return -1;
  }
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  ::GetProcessMemoryInfo(handle.get(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
  return pmc.PrivateUsage;
}

