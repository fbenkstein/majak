// Copyright 2011 Google Inc. All Rights Reserved.
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

#include <ninja/ninja_config.h>

#include <limits.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

#include <getopt.h>

#include <ninja/filesystem.h>
#include <ninja/manifest_parser.h>
#include <ninja/ninja.h>
#include <ninja/version.h>

using namespace ninja;

namespace {
/// Print usage information.
void Usage(const BuildConfig& config) {
  fprintf(
      stderr,
      "usage: ninja [options] [targets...]\n"
      "\n"
      "if targets are unspecified, builds the 'default' target (see manual).\n"
      "\n"
      "options:\n"
      "  --version  print ninja version (\"%s\")\n"
      "\n"
      "  -C DIR   change to DIR before doing anything else\n"
      "  -f FILE  specify input build file [default=build.ninja]\n"
      "\n"
      "  -j N     run N jobs in parallel [default=%d, derived from CPUs "
      "available]\n"
      "  -k N     keep going until N jobs fail (0 means infinity) [default=1]\n"
      "  -l N     do not start new jobs if the load average is greater than N\n"
      "  -n       dry run (don't run commands but act like they succeeded)\n"
      "  -v       show all command lines while building\n"
      "\n"
      "  -d MODE  enable debugging (use '-d list' to list modes)\n"
      "  -t TOOL  run a subtool (use '-t list' to list subtools)\n"
      "    terminates toplevel options; further flags are passed to the tool\n"
      "  -w FLAG  adjust warnings (use '-w list' to list warnings)\n",
      kNinjaVersion, config.parallelism);
}

/// Parse argv for command-line options.
/// Returns an exit code, or -1 if Ninja should continue.
int ReadFlags(int* argc, char*** argv, Options* options, BuildConfig* config) {
  config->parallelism = GuessParallelism();

  enum { OPT_VERSION = 1 };
  const option kLongOptions[] = { { "help", no_argument, nullptr, 'h' },
                                  { "version", no_argument, nullptr,
                                    OPT_VERSION },
                                  { nullptr, 0, nullptr, 0 } };

  int opt;
  while (!options->tool &&
         (opt = getopt_long(*argc, *argv, "d:f:j:k:l:nt:vw:C:h", kLongOptions,
                            nullptr)) != -1) {
    switch (opt) {
    case 'd':
      if (!DebugEnable(optarg))
        return 1;
      break;
    case 'f':
      options->input_file = optarg;
      break;
    case 'j': {
      char* end;
      int value = strtol(optarg, &end, 10);
      if (*end != 0 || value <= 0)
        Fatal("invalid -j parameter");
      config->parallelism = value;
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
      config->failures_allowed = value > 0 ? value : INT_MAX;
      break;
    }
    case 'l': {
      char* end;
      double value = strtod(optarg, &end);
      if (end == optarg)
        Fatal("-l parameter not numeric: did you mean -l 0.0?");
      config->max_load_average = value;
      break;
    }
    case 'n':
      config->dry_run = true;
      break;
    case 't':
      options->tool = ChooseTool(optarg);
      if (!options->tool)
        return 0;
      break;
    case 'v':
      config->verbosity = BuildConfig::VERBOSE;
      break;
    case 'w':
      if (!WarningEnable(optarg, options))
        return 1;
      break;
    case 'C':
      options->working_dir = optarg;
      break;
    case OPT_VERSION:
      printf("%s, actually majak %s (%s)\n", kNinjaVersion, MAJAK_GIT_VERSION,
             MAJAK_GIT_COMMIT_ID);
      return 0;
    case 'h':
    default:
      Usage(*config);
      return 1;
    }
  }
  *argv += optind;
  *argc -= optind;

  return -1;
}

[[noreturn]] void real_main(int argc, char** argv) {
  // Use exit() instead of return in this function to avoid potentially
  // expensive cleanup when destructing NinjaMain.
  BuildConfig config;
  Options options = {};
  options.input_file = "build.ninja";
  options.dupe_edges_should_err = true;

  setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
  const char* ninja_command = argv[0];

  int exit_code = ReadFlags(&argc, &argv, &options, &config);
  if (exit_code >= 0)
    exit(exit_code);

  if (options.working_dir) {
    // The formatting of this string, complete with funny quotes, is
    // so Emacs can properly identify that the cwd has changed for
    // subsequent commands.
    // Don't print this if a tool is being used, so that tool output
    // can be piped into a file without this string showing up.
    if (!options.tool)
      printf("ninja: Entering directory `%s'\n", options.working_dir);
    fs::error_code ec;
    fs::current_path(options.working_dir, ec);
    if (ec) {
      Fatal("chdir to '%s' - %s", options.working_dir, ec.message());
    }
  }

  if (options.tool && options.tool->when == Tool::RUN_AFTER_FLAGS) {
    // None of the RUN_AFTER_FLAGS actually use a NinjaMain, but it's needed
    // by other tools.
    NinjaMain ninja(ninja_command, config);
    exit((ninja.*options.tool->func)(&options, argc, argv));
  }

  // Limit number of rebuilds, to prevent infinite loops.
  const int kCycleLimit = 100;
  for (int cycle = 1; cycle <= kCycleLimit; ++cycle) {
    NinjaMain ninja(ninja_command, config);

    ManifestParserOptions parser_opts;
    if (options.dupe_edges_should_err) {
      parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
    }
    if (options.phony_cycle_should_err) {
      parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
    }
    ManifestParser parser(&ninja.state_, &ninja.disk_interface_, parser_opts);
    std::string err;
    if (!parser.Load(options.input_file, &err)) {
      Error("%s", err.c_str());
      exit(1);
    }

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOAD)
      exit((ninja.*options.tool->func)(&options, argc, argv));

    if (!ninja.EnsureBuildDirExists())
      exit(1);

    if (!ninja.OpenBuildLog())
      exit(1);

    if (options.tool && options.tool->when == Tool::RUN_AFTER_LOGS)
      exit((ninja.*options.tool->func)(&options, argc, argv));

    // Attempt to rebuild the manifest before building anything else
    if (ninja.RebuildManifest(options.input_file, &err)) {
      // In dry_run mode the regeneration will succeed without changing the
      // manifest forever. Better to return immediately.
      if (config.dry_run)
        exit(0);
      // Start the build over with the new manifest.
      continue;
    } else if (!err.empty()) {
      Error("rebuilding '%s': %s", options.input_file, err.c_str());
      exit(1);
    }

    int result = ninja.RunBuild(argc, argv);
    if (g_metrics)
      ninja.DumpMetrics();
    exit(result);
  }

  Error("manifest '%s' still dirty after %d tries\n", options.input_file,
        kCycleLimit);
  exit(1);
}
}  // anonymous namespace

int main(int argc, char** argv) {
  return guarded_main(real_main, argc, argv);
}
