#include "tracer.h"

#include <iostream>
#include <memory>

#include "uwu.h"

#include <json.hpp>

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance,
                    HINSTANCE hPrevInstance,
                    PWSTR pCmdLine,
                    int nCmdShow) {
  auto tracer = std::make_unique<::tracer>();

  uwu::browser_config cfg;
  cfg.title = "LiveTrace";

  uwu::browser browser(cfg);
  // browser.serve("livetrace", "../web/dist", true);
  // browser.navigate("https://livetrace/index.html");
  browser.navigate("http://localhost:5173");
  browser.on_message([&](const std::string& msg) {
    nlohmann::json req = nlohmann::json::parse(msg);

    std::string type = req["type"];
    if (type == "process") {
      std::string rule = req.value("rule", "");

      std::optional<process::process_info> proc;
      auto process_snapshot = process::snapshot();
      if (std::regex_match(rule, std::regex("\\d+"))) {
        auto it = std::find_if(process_snapshot->processes.begin(),
                     process_snapshot->processes.end(),
                     [&](const auto& p) { return p.id == std::stoi(rule); });
        if (it != process_snapshot->processes.end()) {
          proc = *it;
        }
      } else {
        proc = process_snapshot->find(std::regex(rule));
      }

      if (proc && proc->id > 0) {
        tracer->start(proc->id);
      } else {
        tracer->stop();
      }
    } else if (type == "pause") {
      tracer->pause();
    } else if (type == "thread") {
      int thread = req.value("thread", 0);
      tracer->select(thread);
    } else if (type == "snapshot") {
      nlohmann::json data = tracer->snapshot();
      nlohmann::json json = {
          {"type", "snapshot"},
          {"data", data},
      };
      browser.message(json.dump(2));
    }
  });
  browser.devtools();

  uwu::message_loop();

  tracer->stop();

  return 0;
}