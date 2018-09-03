// Copyright 2018 Frank Benkstein All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <cstring>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include "getopt.h"
#else
#include <getopt.h>
#include <unistd.h>
#endif

#include "util.h"
#include "version.h"

constexpr const char MAIN_USAGE[] = R"(usage: majak [options] <command>

options:
  -V --version  print majak version
  -C DIR        change to DIR before doing anything else

commands:
  build    build given targets
  version  print majak version
)";

using Command = int (*)(const char* working_dir, int argc, char** argv);

int CommandBuild(const char* working_dir, int argc, char** argv) {
  if (working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    // Don't print this if a tool is being used, so that tool output
    // can be piped into a file without this string showing up.
    printf("majak: Entering directory `%s'\n", working_dir);
    if (chdir(working_dir) < 0) {
      Fatal("chdir to '%s' - %s", working_dir, strerror(errno));
    }
  }

  fputs("Not implemented\n", stderr);
  return 0;
}

int CommandVersion(const char* = nullptr, int = 0, char** = nullptr) {
  printf("majak %s\n", kNinjaVersion);
  return 0;
}

Command ChooseCommand(const char* command_name) {
  using CommandEntry = std::pair<const char*, Command>;
  static constexpr CommandEntry commands[] = {
    { "build", CommandBuild },
    { "version", CommandVersion },
  };

  if (command_name) {
    auto command = std::find_if(begin(commands), end(commands),
                                [command_name](const CommandEntry& entry) {
                                  return strcmp(entry.first, command_name) == 0;
                                });

    if (command != std::end(commands)) {
      return command->second;
    }
  }

  return nullptr;
}

NORETURN void real_main(int argc, char** argv) {
  const char* working_dir = nullptr;
  optind = 1;
  int opt;

  constexpr option kLongOptions[] = { { "help", no_argument, nullptr, 'h' },
                                      { "version", no_argument, nullptr, 'V' },
                                      { nullptr, 0, nullptr, 0 } };

  while ((opt = getopt_long(argc, argv, "C:hV", kLongOptions, nullptr)) != -1) {
    switch (opt) {
    case 'C':
      working_dir = optarg;
      break;
    case 'V':
      exit(CommandVersion());
    case 'h':
    default:
      fputs(MAIN_USAGE, stderr);
      exit(opt == 'h');
    }
  }

  argv += optind;
  argc -= optind;

  if (argc == 0) {
    fputs(MAIN_USAGE, stderr);
    exit(0);
  }

  if (auto command = ChooseCommand(*argv)) {
    exit(command(working_dir, argc, argv));
  } else {
    fprintf(stderr, "majak: '%s' is not a majak command.  See 'majak -h'.\n",
            *argv);
    exit(1);
  }

  exit(0);
}

#ifdef _MSC_VER
/// This handler processes fatal crashes that you can't catch
/// Test example: C++ exception in a stack-unwind-block
/// Real-world example: ninja launched a compiler to process a tricky
/// C++ input file. The compiler got itself into a state where it
/// generated 3 GB of output and caused ninja to crash.
void TerminateHandler() {
  CreateWin32MiniDump(NULL);
  Fatal("terminate handler called");
}

/// On Windows, we want to prevent error dialogs in case of exceptions.
/// This function handles the exception, and writes a minidump.
int ExceptionFilter(unsigned int code, struct _EXCEPTION_POINTERS* ep) {
  Error("exception: 0x%X", code);  // e.g. EXCEPTION_ACCESS_VIOLATION
  fflush(stderr);
  CreateWin32MiniDump(ep);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif  // _MSC_VER

int main(int argc, char** argv) {
#if defined(_MSC_VER)
  // Set a handler to catch crashes not caught by the __try..__except
  // block (e.g. an exception in a stack-unwind-block).
  std::set_terminate(TerminateHandler);
  __try {
    // Running inside __try ... __except suppresses any Windows error
    // dialogs for errors such as bad_alloc.
    real_main(argc, argv);
  } __except (ExceptionFilter(GetExceptionCode(), GetExceptionInformation())) {
    // Common error situations return exitCode=1. 2 was chosen to
    // indicate a more serious problem.
    return 2;
  }
#else
  real_main(argc, argv);
#endif
}
