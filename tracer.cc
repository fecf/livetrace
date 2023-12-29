#include "tracer.h"

#include <wil/result.h>
#include <wrl/implements.h>
#include <tlhelp32.h>
#include <winrt/base.h>
#include <dbghelp.h>

#include <iostream>
#include <sstream>
#include <cassert>
#include <regex>
#include <vector>
#include <set>
#include <queue>

#pragma comment(lib, "dbgeng.lib")

namespace {

std::string narrow(const std::wstring& str) {
  return winrt::to_string(str);
}
std::wstring widen(const std::string& str) {
  return (std::wstring)winrt::to_hstring(str);
}

}  // namespace

tracer::tracer() {
}

tracer::~tracer() {
  stop();
}

void tracer::worker_thread(int pid) {
  try {
    wil::unique_process_handle handle(
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!handle) {
      throw std::domain_error("failed to OpenProcess().");
    }
    wchar_t name[4096];
    DWORD size = sizeof(name);
    BOOL ret = ::QueryFullProcessImageName(handle.get(), 0, name, &size);
    if (!ret) {
      throw std::domain_error("failed to QueryFullProcessImageName().");
    }
    process_name_ = narrow(name);

    ComPtr<IDebugClient8> debug_client;
    ComPtr<IDebugControl7> debug_control;
    ComPtr<IDebugSystemObjects4> debug_system_objects;
    ComPtr<IDebugSymbols5> debug_symbols;

    HRESULT hr = ::DebugCreate(__uuidof(IDebugClient8), &debug_client);
    THROW_IF_FAILED(hr);

    hr = debug_client.As(&debug_control);
    THROW_IF_FAILED(hr);

    hr = debug_client.As(&debug_system_objects);
    THROW_IF_FAILED(hr);

    hr = debug_client.As(&debug_symbols);
    THROW_IF_FAILED(hr);

    bool attached = false;
    hr = debug_client->AttachProcess(
        0ull, pid,
        DEBUG_ATTACH_NONINVASIVE | DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND);
    THROW_IF_FAILED(hr);

    hr = debug_control->WaitForEvent(DEBUG_WAIT_DEFAULT, INFINITE);
    THROW_IF_FAILED(hr);
    attached = true;

    // main loop
    state_ = running;
    while (!exit_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      if (state_ == paused) {
        continue;
      }

      ULONG total_thread_count = 0, largest_process = 0;
      ret = debug_system_objects->GetTotalNumberThreads(&total_thread_count,
                                                        &largest_process);
      if (FAILED(ret)) {
        continue;
      }

      std::vector<thread> threads;
      for (int i = 0; i < (int)total_thread_count; ++i) {
        ret = debug_system_objects->SetCurrentThreadId(i);
        if (FAILED(ret)) {
          continue;
        }

        // set thread
        ULONG thread_system_id = 0;
        ret = debug_system_objects->GetCurrentThreadSystemId(&thread_system_id);
        if (FAILED(ret)) {
          continue;
        }

        // get cycles
        wil::unique_handle handle(
            ::OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread_system_id));
        unsigned long long cycles = 0;
        ::QueryThreadCycleTime(handle.get(), &cycles);

        // capture stackframe
        std::vector<stack_frame> sf;
        if (thread_system_id == thread_id_) {
          sf = capture_stack_frames(kMaxStackFrames, debug_control.Get(), debug_symbols.Get());
        } else {
          sf =  capture_stack_frames(1, debug_control.Get(), debug_symbols.Get());
        }

        if (!sf.empty()) {
          threads.push_back(thread{
              .id = thread_system_id,
              .cycles = cycles,
              .instruction_offset = sf[0].instruction_offset,
          });
        }

        if (thread_system_id == thread_id_) {
          // capture all stackframes for selected thread
          std::lock_guard lock(mutex_serialize_);
          if (!sf.empty()) {
            for (const auto& sf : sf) {
              inclusive_[thread_id_][sf.instruction_offset]++;
            }
            exclusive_[thread_id_][sf.front().instruction_offset]++;
          }
          stack_frame_ = std::move(sf);
        }
      }

      std::lock_guard lock(mutex_serialize_);
      counter_++;
      threads_ = std::move(threads);
    }

    // finalize stacktrace
    if (debug_client) {
      debug_client->DetachProcesses();
    }
    state_ = exited;
  } catch (std::exception& ex) {
    state_ = failed;
    err_ = ex.what();
  }
}

nlohmann::json tracer::snapshot() {
  std::lock_guard lock(mutex_serialize_);

  nlohmann::json instruction_point_map;
  for (const auto& [key, data] : instruction_point_map_) {
    instruction_point_map[std::to_string(key)] = *data;
  }

  auto now = std::chrono::high_resolution_clock::now();
  auto elapsed = now - start_;
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  nlohmann::json json = {
      // summary
      {"process_id", process_id_},
      {"process_name", process_name_},
      {"process_cpu_usage", monitor_.cpu_usage(process_id_)},
      {"process_phys_mem_usage", monitor_.phys_mem_usage(process_id_)},
      {"process_virt_mem_usage", monitor_.virt_mem_usage(process_id_)},
      {"thread_id", thread_id_},
      {"elapsed", elapsed_ms},
      {"samples", counter_},
      {"state", (int)state_},
      // threads
      {"threads", threads_},
      // selected thread
      {"instruction_point_map", instruction_point_map},
      {"stack_frame", stack_frame_},
      {"inclusive", inclusive_[thread_id_]},
      {"exclusive", exclusive_[thread_id_]},
  };
  return json;
}

void tracer::start(uint32_t pid) {
  stop();

  {
    std::lock_guard lock(mutex_serialize_);
    state_ = state::preparing;
    start_ = std::chrono::high_resolution_clock::now();
    counter_ = 0;
    process_id_ = pid;
    threads_.clear();
    stack_frame_.clear();
    inclusive_.clear();
    exclusive_.clear();
    instruction_point_map_.clear();
  }

  exit_ = false;
  thread_ = std::thread(&tracer::worker_thread, this, pid);
}

void tracer::select(uint32_t tid) {
  thread_id_ = tid;
}

void tracer::pause() {
  if (state_ == running) {
    state_ = paused;
  } else if (state_ == paused) {
    state_ = running;
  }
}

void tracer::stop() {
  if (thread_.joinable()) {
    exit_ = true;
    thread_.join();
  }
  state_ = state::exited;
  process_id_ = 0;
  process_name_ = "";
}

bool tracer::lookup(stack_frame* stack_frame, IDebugSymbols5* debug_symbols_) {
  {
    std::lock_guard lock(mutex_serialize_);
    auto it = instruction_point_map_.find(stack_frame->instruction_offset);
    if (it != instruction_point_map_.end()) {
      stack_frame->ip = it->second.get();
      return true;
    }
  }

  auto ip = std::make_unique<instruction_point>();

  uint8_t buffer[1024];
  ULONG needed = 0;
  ULONG64 displacement = 0;
  HRESULT hr;
  hr = debug_symbols_->GetFunctionEntryByOffset(
      stack_frame->instruction_offset, 0, buffer, sizeof(buffer), &needed);
  if (SUCCEEDED(hr)) {
    wchar_t name[1024 * 2]{};
    ULONG name_size = 0;
    hr = debug_symbols_->GetNameByOffsetWide(stack_frame->instruction_offset,
                                             name, sizeof(name), &name_size,
                                             &displacement);
    if (SUCCEEDED(hr)) {
      if (needed == sizeof(FPO_DATA)) {
        const FPO_DATA* fpo_data = (FPO_DATA*)(buffer);
        ip->address = fpo_data->ulOffStart;
        // todo:
      } else if (needed == sizeof(IMAGE_FUNCTION_ENTRY)) {
        const IMAGE_FUNCTION_ENTRY* image_function_entry = (IMAGE_FUNCTION_ENTRY*)(buffer);
        ip->function_name = narrow(name);
        ip->address = image_function_entry->StartingAddress;
      }
      ip->displacement = displacement;
    }
  } else {
    return false;
  }

  ULONG line = 0;
  wchar_t file_name[1024 * 2];
  ULONG file_name_size = 0;
  hr = debug_symbols_->GetLineByOffsetWide(stack_frame->instruction_offset,
                                           &line, file_name, sizeof(file_name),
                                           &file_name_size, &displacement);
  if (SUCCEEDED(hr)) {
    ip->source_name = narrow(file_name);
    ip->source_line = line;
  }
  stack_frame->ip = ip.get();

  std::lock_guard lock(mutex_serialize_);
  instruction_point_map_.try_emplace(stack_frame->instruction_offset, std::move(ip));
  return true;
}

std::vector<tracer::stack_frame> tracer::capture_stack_frames(
    int fill_frames,
    IDebugControl7* debug_control,
    IDebugSymbols5* debug_symbols) {
  ULONG filled_frames{};
  std::vector<DEBUG_STACK_FRAME_EX> frames(fill_frames);

  HRESULT hr = debug_control->GetStackTraceEx(
      NULL, NULL, NULL, &frames.front(), (int)frames.size(), &filled_frames);
  if (FAILED(hr)) {
    return {};
  }

  // lookup symbols
  std::vector<stack_frame> sfs;
  for (int i = 0; i < (int)filled_frames; ++i) {
    stack_frame sf{};
    sf.instruction_offset = frames[i].InstructionOffset;
    sf.frame_offset = frames[i].FrameOffset;
    sf.frame_number = frames[i].FrameNumber;
    sf.return_offset = frames[i].ReturnOffset;
    sf.stack_offset = frames[i].StackOffset;
    sf.func_table_entry = frames[i].FuncTableEntry;
    sf.is_virtual = frames[i].Virtual;
    lookup(&sf, debug_symbols);
    sfs.emplace_back(sf);
  }

  return sfs;
}

std::optional<process::process_snapshot> process::snapshot() {
  wil::unique_tool_help_snapshot handle;

  do {
    handle.reset(
        ::CreateToolhelp32Snapshot(TH32CS_SNAPALL | TH32CS_SNAPMODULE32, 0));
  } while (!handle || ::GetLastError() == ERROR_BAD_LENGTH);

  if (!handle) {
    return {};
  }

  std::unordered_map<uint32_t, std::vector<thread_info>> thread_info_map;
  THREADENTRY32 te32{sizeof(THREADENTRY32)};
  if (!::Thread32First(handle.get(), &te32)) {
    return {};
  }
  do {
    thread_info ti;
    ti.id = te32.th32ThreadID;
    ti.owner_process_id = te32.th32OwnerProcessID;
    ti.flags = te32.dwFlags;
    ti.base_pri = te32.tpBasePri;
    ti.delta_pri = te32.tpDeltaPri;
    thread_info_map[te32.th32OwnerProcessID].emplace_back(std::move(ti));
  } while (::Thread32Next(handle.get(), &te32));

  PROCESSENTRY32 pe32{sizeof(PROCESSENTRY32)};
  if (!::Process32First(handle.get(), &pe32)) {
    return {};
  }
  process_snapshot ret;
  do {
    ret.timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();

    process_info pi;
    pi.id = pe32.th32ProcessID;
    pi.parent_process_id = pe32.th32ParentProcessID;
    pi.base_pri = pe32.pcPriClassBase;
    pi.flags = pe32.dwFlags;
    pi.module_id = pe32.th32ModuleID;
    pi.exe = narrow(pe32.szExeFile);
    pi.threads = std::move(thread_info_map[pe32.th32ProcessID]);
    ret.processes.emplace_back(std::move(pi));
  } while (::Process32Next(handle.get(), &pe32));

  return ret;
}

wil::unique_process_handle process::start(
    const std::string& cmdline,
    const std::string& cwd, uint32_t flags) {
  STARTUPINFO si{sizeof(STARTUPINFO)};
  PROCESS_INFORMATION pi{};
  BOOL ret = ::CreateProcess(NULL, (LPWSTR)widen(cmdline).c_str(), NULL, NULL, FALSE,
                  flags, NULL, widen(cwd).c_str(), &si, &pi);
  if (!ret) {
    return 0;
  }
  return wil::unique_process_handle(pi.hProcess);
}

std::optional<process::process_info> process::process_snapshot::find(
    const std::regex& re) const {
  for (const auto& process : processes) {
    if (std::regex_search(process.exe, re)) {
      return process;
    }
  }
  return {};
}
