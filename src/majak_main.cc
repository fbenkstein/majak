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

#include "ninja_config.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

#ifdef NINJA_HAVE_GETOPT
#include <getopt.h>
#else
#include "getopt.h"
#endif

#include <flatbuffers/idl.h>

#include "manifest_parser.h"
#include "ninja.h"
#include "util.h"
#include "version.h"

using namespace ninja;

namespace {
constexpr const char kInputFile[] = "build.ninja";

constexpr const char MAIN_USAGE[] = R"(usage: majak [options] <command>

options:
  -V --version  print majak version
  -C DIR        change to DIR before doing anything else

commands:
  build    build given targets
  version  print majak version
  debug    debug commands
)";

constexpr const char BUILD_USAGE[] =
    R"(usage: majak build [options] [targets...]

options:
  -j N     run N jobs in parallel [default derived from CPUs available]
  -k N     keep going until N jobs fail (0 means infinity) [default=1]
  -n       dry run (don't run commands but act like they succeeded)
  -v       show all command lines while building
)";

constexpr const char DEBUG_USAGE[] =
    R"(usage: majak debug <command>

commands:
  dump-build-log   dump the build log
)";

using Command = int (*)(const char* working_dir, int argc, char** argv);
struct CommandEntry {
  std::string_view name;
  Command command;
};

template <size_t N>
Command ChooseCommand(const CommandEntry (&commands)[N],
                      const char* command_name) {
  if (command_name) {
    auto entry = std::find_if(std::begin(commands), std::end(commands),
                              [command_name](const CommandEntry& entry) {
                                return entry.name == command_name;
                              });

    if (entry != std::end(commands)) {
      return entry->command;
    }
  }

  return nullptr;
}

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

    std::string err;
    if (!parser.Load(kInputFile, &err)) {
      Error("%s", err.c_str());
      exit(1);
    }

    if (!ninja.EnsureBuildDirExists())
      exit(1);

    if (!ninja.OpenBuildLog())
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

    int result = ninja.RunBuild(argc, argv, true);
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

int CommandDebugDumpBuildLog(const char* working_dir, int argc, char** argv) {
  if (working_dir && chdir(working_dir) < 0) {
    Fatal("chdir to '%s' - %s", working_dir, strerror(errno));
  }

  std::string log_path = BuildLog::kFilename;

  {
    State state;
    RealDiskInterface disk_interface;
    ManifestParserOptions parser_opts;
    parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
    parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
    ManifestParser parser(&state, &disk_interface, parser_opts);

    std::string err;
    if (!parser.Load(kInputFile, &err)) {
      Error("loading manifest failed: %s", err.c_str());
      exit(1);
    }

    std::string build_dir = state.bindings_->LookupVariable("builddir");
    if (!build_dir.empty())
      log_path = build_dir + "/" + log_path;
  }

  FILE* file = fopen(log_path.c_str(), "rb");
  if (!file) {
    if (errno == ENOENT) {
      std::cout << "<missing>" << std::endl;
      return 0;
    }
    Error("failed to open build log: %s", strerror(errno));
    return 1;
  }

  flatbuffers::IDLOptions idl_options;
  idl_options.strict_json = true;
  idl_options.indent_step = -1;
  flatbuffers::Parser parser(idl_options);

  if (!parser.Parse(BuildLog::kSchema))
    Fatal("invalid schema");

  std::string output;
  uint8_t size_buffer[sizeof(flatbuffers::uoffset_t)];
  std::vector<uint8_t> entry_buffer;

  for (;;) {
    if (fread(size_buffer, 1, sizeof(size_buffer), file) !=
        sizeof(size_buffer)) {
      break;
    }

    size_t entry_size = flatbuffers::GetPrefixedSize(size_buffer);

    entry_buffer.resize(std::max(entry_buffer.size(), entry_size));

    if (fread(entry_buffer.data(), 1, entry_size, file) != entry_size) {
      break;
    }

    flatbuffers::Verifier verifier(entry_buffer.data(), entry_buffer.size());

    if (!verifier.VerifyBuffer<log::EntryHolder>()) {
      Fatal("failed to verify entry");
    }

    if (flatbuffers::GenerateText(parser, entry_buffer.data(), &output)) {
      std::cout << output << std::endl;
    } else {
      Fatal("failed to serialize entry");
    }
    output.clear();
  }

  return 0;
}

int CommandDebug(const char* working_dir, int argc, char** argv) {
  optind = 1;
  int opt;

  constexpr option kLongOptions[] = { { "help", no_argument, nullptr, 'h' },
                                      { nullptr, 0, nullptr, 0 } };

  while ((opt = getopt_long(argc, argv, "+h", kLongOptions, nullptr)) != -1) {
    switch (opt) {
    case 'h':
    default:
      fputs(DEBUG_USAGE, stderr);
      exit(opt == 'h');
    }
  }

  argv += optind;
  argc -= optind;

  if (argc == 0) {
    fputs(DEBUG_USAGE, stderr);
    exit(0);
  }

  static constexpr CommandEntry commands[] = {
    { "dump-build-log", CommandDebugDumpBuildLog },
  };

  if (auto command = ChooseCommand(commands, *argv)) {
    exit(command(working_dir, argc, argv));
  } else {
    fprintf(
        stderr,
        "majak: '%s' is not a majak debug command.  See 'majak debug -h'.\n",
        *argv);
    exit(1);
  }

  exit(0);
  return 0;
}

[[noreturn]] void real_main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

  const char* working_dir = nullptr;
  optind = 1;
  int opt;

  constexpr option kLongOptions[] = { { "help", no_argument, nullptr, 'h' },
                                      { "version", no_argument, nullptr, 'V' },
                                      { nullptr, 0, nullptr, 0 } };

  while ((opt = getopt_long(argc, argv, "+C:hV", kLongOptions, nullptr)) !=
         -1) {
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

  static constexpr CommandEntry commands[] = {
    { "build", CommandBuild },
    { "version", CommandVersion },
    { "debug", CommandDebug },
  };

  if (auto command = ChooseCommand(commands, *argv)) {
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
