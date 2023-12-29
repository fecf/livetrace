#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <queue>

namespace uwu {

struct browser_config {
  const int kDefault = 0x80000000;  // same as CW_USEDEFAULT
  int x = kDefault;
  int y = kDefault;
  int width = kDefault;
  int height = kDefault;
  std::string title = "uwu";

  bool hide = false;
  bool borderless = false;
  bool transparent = false;
  bool blur = false;  // DwmEnableBlurBehindWindow

  bool context_menu = false;  // use edge context menu
};

class browser {
  friend class context;

 public:
  browser();
  browser(const browser_config& config);
  ~browser();

  browser(const browser&) = delete;
  browser& operator=(const browser&) = delete;
  browser(browser&&) = default;
  browser& operator=(browser&&) = default;

  void show();
  void hide();
  void close();
  void minimize();
  void maximize();
  void move(int x, int y);
  void resize(int width, int height);

  bool load(const std::string& html);
  bool navigate(const std::string& uri);

  bool serve(const std::string& domain, const std::string& path, bool live_reload);

  void eval(const std::string& js, std::function<void(const std::string&)> cb = {});
  void devtools();
  void message(const std::string& json);
  void on_message(std::function<void(const std::string&)> cb);

  void dispatch_task(std::function<void()> task);

 public:
  const browser_config config;

 private:
  struct impl;
  std::unique_ptr<impl> d;
};

class shared_buffer {
 public:
  shared_buffer(size_t size);
  ~shared_buffer();

  void* ptr;
  size_t size;

 private:
  struct impl;
  std::unique_ptr<impl> d;
};

void message_loop();

}  // namespace uwu

