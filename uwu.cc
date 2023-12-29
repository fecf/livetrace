#include "uwu.h"

#include <cassert>
#include <iostream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stacktrace>
#include <stdexcept>

#include <dwmapi.h>
#include <webview2.h>
#include <windowsx.h>
#pragma comment(lib, "dwmapi.lib")

#include <wil/com.h>

#include <wrl.h>
using namespace Microsoft::WRL;

#include <winrt/Windows.Data.Json.h>
using namespace winrt::Windows::Data::Json;

namespace uwu {

constexpr const wchar_t* kScript =
    LR"js(
window.uwu = {
  post: (json) => window.chrome.webview.postMessage(json),
  watch: (cb) => window.chrome.webview.addEventListener('message', cb),
  unwatch: (cb) => window.chrome.webview.removeEventListener('message', cb),
};
)js";

void __stdcall wil_logging_callback(const wil::FailureInfo& fi) noexcept;

class context {
 public:
  ComPtr<ICoreWebView2Environment12> environment;
  std::map<uwu::browser*, uwu::browser::impl*> browsers;

  context(const context&) = delete;
  context& operator=(const context&) = delete;
  context(context&&) = default;
  context& operator=(context&&) = default;

  context() {
    wil::SetResultLoggingCallback(wil_logging_callback);

    HRESULT hr;
    hr = ::OleInitialize(NULL);
    THROW_IF_FAILED(hr);

    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    hr = ::CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT hr, ICoreWebView2Environment* ret) {
              if (FAILED(hr)) {
                return hr;
              }
              ret->QueryInterface<ICoreWebView2Environment12>(&environment);
              return S_OK;
            })
            .Get());
    THROW_IF_FAILED(hr);
  }

  ~context() {
    environment.Reset();
    ::OleUninitialize();
  }
};

context& ctx() {
  static context ctx;
  return ctx;
}

void __stdcall wil_logging_callback(const wil::FailureInfo& fi) noexcept {
  const std::wstring log_format = L"{} {}:{} hr={} msg={}";
  // std::wstring msg = std::format(log_format, fi.pszFunction, fi.pszFile, fi.uLineNumber, fi.hr, fi.pszMessage);
  for (auto& [browser, impl] : uwu::ctx().browsers) {
    // eval(std::format("console.error('{}')", msg));
  }
}

std::wstring widen(const std::string& str) {
  return (std::wstring)winrt::to_hstring(str);
}
std::string narrow(const std::wstring& str) {
  return winrt::to_string(str);
}

class file_watcher {
 public:
  file_watcher(const std::string& path, std::function<void()> cb)
      : path_(path), cb_(cb), handle_() {
    // TODO: ReadDirectoryChangesW
    handle_ = ::FindFirstChangeNotification(widen(path).c_str(), TRUE,
                                            FILE_NOTIFY_CHANGE_LAST_WRITE);
    if (handle_ == NULL) {
      LOG_NTSTATUS(::GetLastError());
    }

    exit_ = false;
    thread_ = std::thread([&] {
      while (exit_) {
        DWORD ret = ::WaitForSingleObject(handle_, 0);
        if (ret == WAIT_OBJECT_0) {
          BOOL ret = ::FindNextChangeNotification(handle_);
          if (ret) {
            assert(cb_);
            cb_();
          } else {
            LOG_NTSTATUS(::GetLastError());
            handle_ = NULL;
            return;
          }
        } else if (ret == WAIT_ABANDONED || ret == WAIT_FAILED) {
          handle_ = NULL;
        }
      }
    });
  }

  ~file_watcher() {
    if (handle_ != NULL) {
      ::FindCloseChangeNotification(handle_);
    }
    if (thread_.joinable()) {
      exit_ = true;
      thread_.join();
    }
  }

 private:
  std::string path_;
  std::thread thread_;
  std::function<void()> cb_;
  std::atomic_bool exit_;
  HANDLE handle_;
};

struct browser::impl {
  browser* parent = NULL;
  HWND hwnd = NULL;
  ComPtr<ICoreWebView2_19> webview;
  ComPtr<ICoreWebView2Controller4> controller;
  ComPtr<ICoreWebView2Settings> settings;

  std::vector<std::function<void(const std::string&)>> message_cb;
  std::vector<std::unique_ptr<file_watcher>> watcher;
  std::queue<std::function<void()>> task_queue;

  static LRESULT CALLBACK staticWndproc(HWND hwnd,
                                        UINT msg,
                                        WPARAM wp,
                                        LPARAM lp) {
    if (msg == WM_CREATE) {
      CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lp);
      ::SetWindowLongPtr(hwnd, GWLP_USERDATA,
                         reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    LONG_PTR ptr = ::GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (ptr != NULL) {
      browser* w = reinterpret_cast<uwu::browser*>(ptr);
      return w->d->wndproc(hwnd, msg, wp, lp);
    } else {
      return ::DefWindowProc(hwnd, msg, wp, lp);
    }
  };

  LRESULT wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE) {
      RECT bounds;
      ::GetClientRect(hwnd, &bounds);
      if (controller) {
        controller->put_Bounds(bounds);
      }
    } else if (msg == WM_CREATE) {
      uwu::ctx().browsers.emplace(parent, this);
    } else if (msg == WM_CLOSE) {
      ::DestroyWindow(hwnd);
    } else if (msg == WM_DESTROY) {
      uwu::ctx().browsers.erase(parent);
      if (uwu::ctx().browsers.empty()) {
        ::PostQuitMessage(0);
      }
    } else if (msg == WM_DPICHANGED) {
      const RECT* size = reinterpret_cast<RECT*>(lp);
      ::SetWindowPos(hwnd, nullptr, size->left, size->top,
                     size->right - size->left, size->bottom - size->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      return TRUE;
    } else if (msg == WM_NCACTIVATE && parent->config.borderless) {
      return ::DefWindowProc(hwnd, msg, wp, -1);
    } else if (msg == WM_NCCALCSIZE && parent->config.borderless &&
               wp == TRUE) {
      // taken from
      // https://stackoverflow.com/questions/39731497/create-window-without-titlebar-with-resizable-border-and-without-bogus-6px-whit

      NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
      WINDOWPLACEMENT pl{};
      ::GetWindowPlacement(hwnd, &pl);
      if (pl.showCmd == SW_MAXIMIZE) {
        HMONITOR monitor = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
        if (monitor) {
          MONITORINFO mi{sizeof(mi)};
          if (::GetMonitorInfo(monitor, &mi)) {
            ::SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                           mi.rcMonitor.right - mi.rcMonitor.left,
                           mi.rcMonitor.bottom - mi.rcMonitor.top, 0);
            params->rgrc[0] = mi.rcMonitor;
          }
        }
      } else {
        params->rgrc[0].top += 1;
        params->rgrc[0].left += 8;
        params->rgrc[0].right -= 8;
        params->rgrc[0].bottom -= 8;
      }
      return 0;
    } else if (msg == WM_APP) {
      while (task_queue.size()) {
        auto task = std::move(task_queue.front());
        task_queue.pop();
        task();
      }
    } else {
      return ::DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
  }

  void process_message(const std::wstring& json) {
    JsonObject obj;
    if (!JsonObject::TryParse(json, obj)) {
      // assert(false && "failed to JsonObject::TryParse().");
      return;
    }

    if (!obj.TryLookup(L"__event")) {
      return;
    }

    std::wstring event = (std::wstring)obj.GetNamedString(L"__event");
    if (event == L"show") {
      parent->show();
    } else if (event == L"hide") {
      parent->hide();
    } else if (event == L"close") {
      parent->close();
    } else if (event == L"minimize") {
      parent->minimize();
    } else if (event == L"maximize") {
      parent->maximize();
    } else if (event == L"resize") {
      int width = (int)obj.GetNamedNumber(L"width");
      int height = (int)obj.GetNamedNumber(L"height");
      if (width > 0 && height > 0) {
        parent->resize(width, height);
      }
    } else if (event == L"move") {
      int x = (int)obj.GetNamedNumber(L"x");
      int y = (int)obj.GetNamedNumber(L"y");
      bool drag = (bool)obj.GetNamedBoolean(L"drag");
      if (drag) {
        POINT pt{};
        ::GetCursorPos(&pt);
        parent->move(pt.x - x, pt.y - y);
      } else {
        parent->move(x, y);
      }
    }
  }
};

const DWORD borderless_fullscreen =
    WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX;

browser::browser() : browser(browser_config()) {}

browser::browser(const browser_config& config)
    : d(std::make_unique<impl>()), config(config) {
  d->parent = this;

  const wchar_t* kClassName = L"uwu";
  static std::once_flag once;
  std::call_once(once, [&] {
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = browser::impl::staticWndproc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = ::GetModuleHandle(NULL);
    wcex.hIcon = NULL;
    wcex.hIconSm = NULL;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = kClassName;
    ::RegisterClassExW(&wcex);
  });

  DWORD exstyle = WS_EX_CONTROLPARENT;
  DWORD style = WS_OVERLAPPEDWINDOW;
  if (config.transparent) {
    exstyle |= WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_COMPOSITED;
  }

  HWND hwnd = ::CreateWindowEx(exstyle, kClassName, widen(config.title).c_str(),
                               style, config.x, config.y, config.width,
                               config.height, nullptr, nullptr, nullptr, this);

  if (hwnd == NULL) {
    throw std::runtime_error("failed to CreateWindowEx().");
  }
  d->hwnd = hwnd;

  if (config.borderless) {
    LONG_PTR style = ::GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~WS_OVERLAPPEDWINDOW;
    style |= WS_VISIBLE | WS_THICKFRAME | WS_POPUP | WS_CAPTION;
    ::SetWindowLongPtr(hwnd, GWL_STYLE, style);
    ::SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
  }

  if (config.blur) {
    DWM_BLURBEHIND bb{};
    bb.dwFlags = DWM_BB_ENABLE;
    bb.fEnable = true;
    bb.hRgnBlur = NULL;
    HRESULT hr = ::DwmEnableBlurBehindWindow(hwnd, &bb);
    LOG_IF_FAILED(hr);
  }

  ::ShowWindow(hwnd, SW_SHOWMINIMIZED);

  bool ready = false;
  HRESULT hr = uwu::ctx().environment->CreateCoreWebView2Controller(
      hwnd,
      Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [=, &ready](HRESULT hr, ICoreWebView2Controller* c) -> HRESULT {
            RETURN_IF_FAILED(hr);
            ComPtr<ICoreWebView2Controller4> controller;
            RETURN_IF_FAILED(
                c->QueryInterface<ICoreWebView2Controller4>(&controller));
            d->controller = controller;

            ComPtr<ICoreWebView2> temp;
            RETURN_IF_FAILED(controller->get_CoreWebView2(&temp));
            ComPtr<ICoreWebView2_19> webview;
            RETURN_IF_FAILED(temp.As(&webview));
            d->webview = webview;

            ComPtr<ICoreWebView2Settings> settings;
            RETURN_IF_FAILED(webview->get_Settings(&settings));
            RETURN_IF_FAILED(settings->put_IsScriptEnabled(TRUE));
            RETURN_IF_FAILED(
                settings->put_AreDefaultScriptDialogsEnabled(TRUE));
            RETURN_IF_FAILED(settings->put_IsWebMessageEnabled(TRUE));
            d->settings = settings;

            RECT bounds{};
            ::GetClientRect(hwnd, &bounds);
            RETURN_IF_FAILED(controller->put_Bounds(bounds));

            if (config.transparent) {
              RETURN_IF_FAILED(controller->put_DefaultBackgroundColor(
                  COREWEBVIEW2_COLOR{0, 255, 255, 255}));
            }

            ::ShowWindow(hwnd, SW_SHOWNORMAL);

            EventRegistrationToken token;
            RETURN_IF_FAILED(webview->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                    [this](ICoreWebView2* sender,
                           ICoreWebView2WebMessageReceivedEventArgs* args) {
                      wil::unique_cotaskmem_string tmp;
                      HRESULT hr = args->get_WebMessageAsJson(&tmp);
                      LOG_IF_FAILED(hr);
                      if (SUCCEEDED(hr)) {
                        d->process_message(tmp.get());

                        for (const auto& cb : d->message_cb) {
                          cb(narrow(tmp.get()));
                        }
                      }
                      return S_OK;
                    })
                    .Get(),
                &token));
            RETURN_IF_FAILED(
                webview->AddScriptToExecuteOnDocumentCreated(kScript, nullptr));
            ready = true;

            return S_OK;
          })
          .Get());
  THROW_IF_FAILED(hr);

  MSG msg{};
  while (!ready && ::GetMessage(&msg, nullptr, 0, 0)) {
    if (msg.message == WM_QUIT) {
      return;
    }
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}

browser::~browser() {}

void browser::show() {
  assert(d->hwnd);
  ::ShowWindow(d->hwnd, SW_SHOWNORMAL);
}

void browser::hide() {
  assert(d->hwnd);
  ::ShowWindow(d->hwnd, SW_HIDE);
}

void browser::close() {
  assert(d->hwnd);
  ::CloseWindow(d->hwnd);
}

void browser::minimize() {
  assert(d->hwnd);
  WINDOWPLACEMENT pl{};
  if (::GetWindowPlacement(d->hwnd, &pl)) {
    if (pl.showCmd == SW_MINIMIZE) {
      ::ShowWindow(d->hwnd, SW_SHOWNORMAL);
    } else {
      ::ShowWindow(d->hwnd, SW_MINIMIZE);
    }
  }
}

void browser::maximize() {
  assert(d->hwnd);
  WINDOWPLACEMENT pl{};
  if (::GetWindowPlacement(d->hwnd, &pl)) {
    if (pl.showCmd == SW_MAXIMIZE) {
      ::ShowWindow(d->hwnd, SW_SHOWNORMAL);
    } else {
      ::ShowWindow(d->hwnd, SW_MAXIMIZE);
    }
  }
}

void browser::move(int x, int y) {
  assert(d->hwnd);
  ::SetWindowPos(d->hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

void browser::resize(int width, int height) {
  assert(d->hwnd);
  ::SetWindowPos(d->hwnd, HWND_TOP, 0, 0, width, height, SWP_NOMOVE);
}

bool browser::load(const std::string& html) {
  assert(d->webview);
  HRESULT hr = d->webview->NavigateToString(widen(html).c_str());
  LOG_IF_FAILED(hr);
  return SUCCEEDED(hr);
}

bool browser::navigate(const std::string& url) {
  assert(d->webview);
  HRESULT hr = d->webview->Navigate(widen(url).c_str());
  LOG_IF_FAILED(hr);
  return SUCCEEDED(hr);
}

bool browser::serve(const std::string& domain,
                    const std::string& path,
                    bool live_reload) {
  std::error_code ec;
  std::filesystem::path target = std::filesystem::absolute(path, ec);
  if (target.empty() || ec) {
    return false;
  }
  if (!std::filesystem::exists(target, ec) || ec) {
    return false;
  }

  assert(d->webview);
  HRESULT hr = d->webview->SetVirtualHostNameToFolderMapping(
      widen(domain).c_str(), target.wstring().c_str(),
      COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
  if (!SUCCEEDED(hr)) {
    LOG_IF_FAILED(hr);
    return false;
  }

  if (live_reload) {
    auto watcher = std::make_unique<file_watcher>(
        path, std::bind(&browser::dispatch_task, this, [=] {
          wil::unique_cotaskmem_string tmp;
          HRESULT hr = d->webview->get_Source(&tmp);
          FAIL_FAST_IF_FAILED(hr);

          std::wstring_view view = tmp.get();
          if (view.starts_with(L"https://" + widen(domain)) ||
              view.starts_with(L"http://" + widen(domain))) {
            HRESULT hr = d->webview->Reload();
            FAIL_FAST_IF_FAILED(hr);
          }
        }));
    d->watcher.emplace_back(std::move(watcher));
  }
  return SUCCEEDED(hr);
}

void browser::eval(const std::string& js,
                   std::function<void(const std::string&)> completed_cb) {
  assert(d->webview);
  HRESULT hr = d->webview->ExecuteScript(
      widen(js).c_str(),
      Callback<ICoreWebView2ExecuteScriptCompletedHandler>([=](HRESULT hr,
                                                               LPCWSTR result) {
        LOG_IF_FAILED(hr);
        if (completed_cb) {
          completed_cb(narrow(result));
        }
        return S_OK;
      }).Get());
  LOG_IF_FAILED(hr);
}

void browser::devtools() {
  d->webview->OpenDevToolsWindow();
}

void browser::message(const std::string& json) {
  assert(d->webview);
  HRESULT hr = d->webview->PostWebMessageAsJson(widen(json).c_str());
  LOG_IF_FAILED(hr);
}

void browser::on_message(std::function<void(const std::string&)> cb) {
  d->message_cb.push_back(cb);
}

void browser::dispatch_task(std::function<void()> task) {
  d->task_queue.push(std::move(task));
  ::SendMessage(d->hwnd, WM_APP, 0, 0);
}

struct shared_buffer::impl {
  ComPtr<ICoreWebView2SharedBuffer> shared_buffer;
};

shared_buffer::shared_buffer(size_t size) : d(std::make_unique<impl>()) {
  HRESULT hr =
      uwu::ctx().environment->CreateSharedBuffer(size, &d->shared_buffer);
  THROW_IF_FAILED(hr);

  hr = d->shared_buffer->get_Buffer((BYTE**)&ptr);
  THROW_IF_FAILED(hr);

  hr = d->shared_buffer->get_Size(&size);
  THROW_IF_FAILED(hr);
}

shared_buffer::~shared_buffer() {}

void message_loop() {
  MSG msg{};
  BOOL ret = FALSE;
  while ((ret = ::GetMessage(&msg, nullptr, 0, 0)) != 0) {
    if (ret == -1 || msg.message == WM_QUIT) {
      return;
    } else {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }
  }
}

}  // namespace uwu
