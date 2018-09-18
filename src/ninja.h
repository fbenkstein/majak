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

#pragma once

#include "build.h"
#include "build_log.h"
#include "deps_log.h"
#include "disk_interface.h"
#include "state.h"

#ifdef _MSC_VER
// Defined in msvc_helper_main-win32.cc.
int MSVCHelperMain(int argc, char** argv);

// Defined in minidump-win32.cc.
void CreateWin32MiniDump(_EXCEPTION_POINTERS* pep);
#endif

namespace ninja {
struct Tool;

/// Command-line options.
struct Options {
  /// Build file to load.
  const char* input_file;

  /// Directory to change into before running.
  const char* working_dir;

  /// Tool to run rather than building.
  const Tool* tool;

  /// Whether duplicate rules for one target should warn or print an error.
  bool dupe_edges_should_err;

  /// Whether phony cycles should warn or print an error.
  bool phony_cycle_should_err;
};

/// The Ninja main() loads up a series of data structures; various tools need
/// to poke into these, so store them as fields on an object.
struct NinjaMain : public BuildLogUser {
  NinjaMain(const char* ninja_command, const BuildConfig& config)
      : ninja_command_(ninja_command), config_(config) {}

  /// Command line used to run Ninja.
  const char* ninja_command_;

  /// Build configuration set from flags (e.g. parallelism).
  const BuildConfig& config_;

  /// Loaded state (rules, nodes).
  State state_;

  /// Functions for accesssing the disk.
  RealDiskInterface disk_interface_;

  /// The build directory, used for storing the build log etc.
  std::string build_dir_;

  BuildLog build_log_;
  DepsLog deps_log_;

  /// The type of functions that are the entry points to tools (subcommands).
  typedef int (NinjaMain::*ToolFunc)(const Options*, int, char**);

  /// Get the Node for a given command-line path, handling features like
  /// spell correction.
  /// If \a source_dwim is true then nodes without inputs will be replaced by
  /// there first output.
  Node* CollectTarget(const char* cpath, bool source_dwim, std::string* err);
  Node* CollectTarget(const char* cpath, std::string* err);

  /// CollectTarget for all command-line arguments, filling in \a targets.
  /// @see CollectTarget for the meaning of \a source_dwim
  bool CollectTargetsFromArgs(int argc, char* argv[],
                              std::vector<Node*>* targets, std::string* err);
  bool CollectTargetsFromArgs(int argc, char* argv[], bool source_dwim,
                              std::vector<Node*>* targets, std::string* err);

  // The various subcommands, run via "-t XXX".
  int ToolGraph(const Options* options, int argc, char* argv[]);
  int ToolQuery(const Options* options, int argc, char* argv[]);
  int ToolDeps(const Options* options, int argc, char* argv[]);
  int ToolBrowse(const Options* options, int argc, char* argv[]);
  int ToolMSVC(const Options* options, int argc, char* argv[]);
  int ToolTargets(const Options* options, int argc, char* argv[]);
  int ToolCommands(const Options* options, int argc, char* argv[]);
  int ToolClean(const Options* options, int argc, char* argv[]);
  int ToolCompilationDatabase(const Options* options, int argc, char* argv[]);
  int ToolRecompact(const Options* options, int argc, char* argv[]);
  int ToolUrtle(const Options* options, int argc, char** argv);

  /// Open the build log.
  /// @return false on error.
  bool OpenBuildLog(bool recompact_only = false);

  /// Open the deps log: load it, then open for writing.
  /// @return false on error.
  bool OpenDepsLog(bool recompact_only = false);

  /// Ensure the build directory exists, creating it if necessary.
  /// @return false on error.
  bool EnsureBuildDirExists();

  /// Rebuild the manifest, if necessary.
  /// Fills in \a err on error.
  /// @return true if the manifest was rebuilt.
  bool RebuildManifest(const char* input_file, std::string* err);

  /// Build the targets listed on the command line.
  /// @see CollectTarget for the meaning of \a source_dwim
  /// @return an exit code.
  int RunBuild(int argc, char** argv, bool source_dwim = false);

  /// Dump the output requested by '-d stats'.
  void DumpMetrics();

  bool IsPathDead(std::string_view s) const override;
};

/// Subtools, accessible via "-t foo".
struct Tool {
  /// Short name of the tool.
  const char* name;

  /// Description (shown in "-t list").
  const char* desc;

  /// When to run the tool.
  enum {
    /// Run after parsing the command-line flags and potentially changing
    /// the current working directory (as early as possible).
    RUN_AFTER_FLAGS,

    /// Run after loading build.ninja.
    RUN_AFTER_LOAD,

    /// Run after loading the build/deps logs.
    RUN_AFTER_LOGS,
  } when;

  /// Implementation of the tool.
  NinjaMain::ToolFunc func;
};

/// Choose a default value for the -j (parallelism) flag.
int GuessParallelism();

/// Find the function to execute for \a tool_name and return it via \a func.
/// Returns a Tool, or nullptr if Ninja should exit.
const Tool* ChooseTool(const std::string& tool_name);

/// Enable a debugging mode.  Returns false if Ninja should exit instead
/// of continuing.
bool DebugEnable(const std::string& name);

/// Set a warning flag.  Returns false if Ninja should exit instead  of
/// continuing.
bool WarningEnable(const std::string& name, Options* options);

using MainFunction = void (*)(int argc, char** argv);

/// Call main while guarded against C++ and Windows exceptions.
int guarded_main(MainFunction real_main, int argc, char** argv);
}  // namespace ninja
