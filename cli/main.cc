#include <iostream>
#include <string>

#include <CLI/CLI.hpp>

#include "taskqueue/version.h"

int main(int argc, char** argv) {
  CLI::App app{"taskq - distributed task queue CLI"};
  app.set_version_flag("-V,--version", std::string(tq::kVersionString));

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (app.get_subcommands().empty()) {
    std::cout << app.help() << '\n';
  }

  return 0;
}
