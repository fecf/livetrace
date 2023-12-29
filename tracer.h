#pragma once

#include <atomic>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>
#include <stack>
#include <set>

#include <dbgeng.h>
#include <wrl.h>
using namespace Microsoft::WRL;
#include <wil/resource.h>

#include <json.hpp>

#include "monitor.h"

namespace nlohmann {

template <typename T>
struct adl_serializer<std::map<int, T>> {
  static void from_json(const json& j, std::map<int, T>& map) {}
  static void to_json(json& result, const std::map<int, T>& map) {
    result = json::object();
    for (const auto& [key, value] : map)
      result[key] = value;
  }
};

template <typename T>
struct adl_serializer<std::map<uint64_t, T>> {
  static void from_json(const json& j, std::map<uint64_t, T>& map) {}
  static void to_json(json& result, const std::map<uint64_t, T>& map) {
    result = json::object();
    for (const auto& [key, value] : map)
      result[std::to_string(key)] = value;
  }
};

}  // namespace nlohmann

class tracer {
 public:
  enum state {
    preparing, running, exited, failed, paused
  };

  struct thread {
    uint32_t id;
    uint64_t cycles;
    uint64_t instruction_offset;
  };
  struct instruction_point {
    std::string source_name;
    uint64_t source_line;
    std::string function_name;
    uint64_t address;
    uint64_t displacement;
  };
  struct stack_frame {
    uint64_t instruction_offset;
    uint64_t return_offset;
    uint64_t frame_offset;
    uint64_t stack_offset;
    uint64_t func_table_entry;
    bool is_virtual;
    uint32_t frame_number;
    instruction_point* ip;
  };

  tracer();
  ~tracer();

  void start(uint32_t pid);
  void select(uint32_t tid);
  void stop();
  void pause();
  nlohmann::json snapshot();

 private:
  void worker_thread(int pid);
  bool lookup(stack_frame* stack_frame, IDebugSymbols5* debug_symbols);
  std::vector<stack_frame> capture_stack_frames(int fill_frames,
                                                IDebugControl7* debug_control,
                                                IDebugSymbols5* debug_symbols);

 private:
  Monitor monitor_;

  int process_id_;
  int thread_id_;

  std::thread thread_;
  std::atomic<bool> exit_;
  std::atomic<state> state_ = state::preparing;
  std::string err_;

  std::string process_name_;
  std::chrono::high_resolution_clock::time_point start_;
  uint64_t counter_;

  std::mutex mutex_serialize_;
  std::vector<thread> threads_;
  std::vector<stack_frame> stack_frame_;
  std::map<uint32_t, std::map<uint64_t, uint64_t>> inclusive_;
  std::map<uint32_t, std::map<uint64_t, uint64_t>> exclusive_;
  std::map<uint64_t, std::unique_ptr<instruction_point>> instruction_point_map_;

  const int kMaxStackFrames = 256;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(tracer::thread,
                                   id,
                                   cycles,
                                   instruction_offset);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(tracer::instruction_point,
                                   source_name,
                                   source_line,
                                   function_name,
                                   address,
                                   displacement);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(tracer::stack_frame,
                                   instruction_offset,
                                   return_offset,
                                   frame_offset,
                                   stack_offset,
                                   func_table_entry,
                                   is_virtual,
                                   frame_number);

class process {
 public:
  struct thread_info {
    uint32_t id;
    uint32_t owner_process_id;
    uint32_t base_pri;
    uint32_t delta_pri;
    uint32_t flags;
  };

  struct process_info {
    uint32_t id;
    uint32_t parent_process_id;
    uint32_t module_id;
    uint32_t base_pri;
    uint32_t flags;
    std::string exe;
    std::vector<thread_info> threads;
  };

  struct process_snapshot {
    uint64_t timestamp;  // ns
    std::vector<process_info> processes;
    std::optional<process_info> find(const std::regex& re) const;
  };

  static std::optional<process_snapshot> snapshot();
  wil::unique_process_handle start(const std::string& cmdline,
                                   const std::string& cwd = {},
                                   uint32_t flags = 0);
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(process::thread_info,
                                   id,
                                   owner_process_id,
                                   base_pri,
                                   delta_pri,
                                   flags);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(process::process_info,
                                   id,
                                   parent_process_id,
                                   module_id,
                                   base_pri,
                                   flags,
                                   exe,
                                   threads);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(process::process_snapshot,
                                   timestamp,
                                   processes);
