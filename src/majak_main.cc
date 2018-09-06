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
#include <climits>
#include <cstring>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include "getopt.h"
#else
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#endif

#include "manifest_parser.h"
#include "ninja.h"
#include "util.h"
#include "version.h"

namespace {
constexpr const char kInputFile[] = "build.ninja";

constexpr const char MAIN_USAGE[] = R"(usage: majak [options] <command>

options:
  -V --version  print majak version
  -C DIR        change to DIR before doing anything else

commands:
  build    build given targets
  version  print majak version
)";

constexpr const char BUILD_USAGE[] =
    R"(usage: majak build [options] [targets...]

options:
  -j N     run N jobs in parallel [default derived from CPUs available]
  -k N     keep going until N jobs fail (0 means infinity) [default=1]
  -n       dry run (don't run commands but act like they succeeded)
  -v       show all command lines while building
)";

int CommandBuild(const char* working_dir, int argc, char** argv) {
  BuildConfig config;
  config.parallelism = GuessParallelism();
  optind = 1;
  int opt;

  constexpr option kLongOptions[] = { { "help", no_argument, nullptr, 'h' },
                                      { nullptr, 0, nullptr, 0 } };

  while ((opt = getopt_long(argc, argv, "j:k:nvh", kLongOptions, nullptr)) !=
         -1) {
    switch (opt) {
    case 'j': {
      char* end;
      int value = strtol(optarg, &end, 10);
      if (*end != 0 || value <= 0)
        Fatal("invalid -j parameter");
      config.parallelism = value;
      break;
    }
    case 'k': {
      char* end;
      int value = strtol(optarg, &end, 10);
      if (*end != 0)
        Fatal("-k parameter not numeric; did you mean -k 0?");

      // We want to go until N jobs fail, which means we should allow
      // N failures and then stop.  For N <= 0, INT_MAX is close enough
      // to infinite for most sane builds.
      config.failures_allowed = value > 0 ? value : INT_MAX;
      break;
    }
    case 'n':
      config.dry_run = true;
      break;
    case 'v':
      config.verbosity = BuildConfig::VERBOSE;
      break;
    case 'h':
    default:
      fputs(BUILD_USAGE, stderr);
      exit(opt == 'h');
    }
  }
  argv += optind;
  argc -= optind;

  // If build.ninja is not found in the current working directory, walk up the
  // directory hierarchy until a build.ninja is found.
  std::string fallback_dir;

  if (!working_dir) {
    RealDiskInterface disk_interface;
    std::string err;

    if (disk_interface.Stat(kInputFile, &err) == 0) {
      fallback_dir = GetCwd(&err);

      if (fallback_dir.empty()) {
        Error("cannot determine working directory: %s", err.c_str());
        return 1;
      }

      uint64_t slash_bits;

      if (!CanonicalizePath(&fallback_dir, &slash_bits, &err)) {
        Error("failed to canonicalize '%s': %s", fallback_dir.c_str(),
              err.c_str());
        return 1;
      }

      size_t pos = std::string::npos;

      while (fallback_dir.back() != '/') {
        pos = fallback_dir.find_last_of('/', pos);

        if (pos == std::string::npos) {
          break;
        }

        fallback_dir.resize(pos + 1);

        bool found = disk_interface.Stat(fallback_dir + kInputFile, &err) > 0;

        if (pos > 0) {
          fallback_dir.resize(pos);
        }

        if (found) {
          working_dir = fallback_dir.c_str();
          break;
        }
      }
    }
  }

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

  constexpr int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {
    NinjaMain ninja("majak build", config);
    ManifestParserOptions parser_opts;
    parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
    parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
    ManifestParser parser(&ninja.state_, &ninja.disk_interface_, parser_opts);

    string err;
    if (!parser.Load(kInputFile, &err)) {
      Error("%s", err.c_str());
      exit(1);
    }

    if (!ninja.EnsureBuildDirExists())
      exit(1);

    if (!ninja.OpenBuildLog() || !ninja.OpenDepsLog())
      exit(1);

    // Attempt to rebuild the manifest before building anything else
    if (ninja.RebuildManifest(kInputFile, &err)) {
      // In dry_run mode the regeneration will succeed without changing the
      // manifest forever. Better to return immediately.
      if (config.dry_run)
        exit(0);
      // Start the build over with the new manifest.
      continue;
    } else if (!err.empty()) {
      Error("rebuilding '%s': %s", kInputFile, err.c_str());
      exit(1);
    }

    int result = ninja.RunBuild(argc, argv);
    if (g_metrics)
      ninja.DumpMetrics();
    exit(result);
  }

  Error("manifest '%s' still dirty after %d tries\n", kInputFile, kCycleLimit);
  return 0;
}

int CommandVersion(const char* = nullptr, int = 0, char** = nullptr) {
  printf("majak %s\n", kNinjaVersion);
  return 0;
}

using Command = int (*)(const char* working_dir, int argc, char** argv);

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
  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

  const char* working_dir = nullptr;
  optind = 1;
  int opt;

  constexpr option kLongOptions[] = { { "help", no_argument, nullptr, 'h' },
                                      { "version", no_argument, nullptr, 'V' },
                                      { nullptr, 0, nullptr, 0 } };

  while ((opt = getopt_long(argc, argv, "+C:hV", kLongOptions, nullptr)) != -1) {
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
}  // anonymous namespace

int main(int argc, char** argv) {
  return guarded_main(real_main, argc, argv);
}
