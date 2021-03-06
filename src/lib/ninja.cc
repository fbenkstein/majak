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

#include <ninja/browse.h>
#include <ninja/build.h>
#include <ninja/build_log.h>
#include <ninja/clean.h>
#include <ninja/debug_flags.h>
#include <ninja/disk_interface.h>
#include <ninja/graph.h>
#include <ninja/graphviz.h>
#include <ninja/manifest_parser.h>
#include <ninja/metrics.h>
#include <ninja/ninja.h>
#include <ninja/state.h>
#include <ninja/util.h>
#include <ninja/version.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <getopt.h>

namespace ninja {
bool NinjaMain::IsPathDead(std::string_view s) const {
  Node* n = state_.LookupNode(s);
  if (!n || !n->in_edge())
    return false;
  // Just checking n isn't enough: If an old output is both in the build log
  // and in the deps log, it will have a Node object in state_.  (It will also
  // have an in edge if one of its inputs is another output that's in the deps
  // log, but having a deps edge product an output thats input to another deps
  // edge is rare, and the first recompaction will delete all old outputs from
  // the deps log, and then a second recompaction will clear the build log,
  // which seems good enough for this corner case.)
  // Do keep entries around for files which still exist on disk, for
  // generators that want to use this information.
  std::string err;
  TimeStamp mtime = disk_interface_.Stat(std::string(s), &err);
  if (mtime == -1)
    Error("%s", err.c_str());  // Log and ignore Stat() errors.
  return mtime == 0;
}

int GuessParallelism() {
  switch (int processors = GetProcessorCount()) {
  case 0:
  case 1:
    return 2;
  case 2:
    return 3;
  default:
    return processors + 2;
  }
}

/// Rebuild the build manifest, if necessary.
/// Returns true if the manifest was rebuilt.
bool NinjaMain::RebuildManifest(const char* input_file, std::string* err) {
  std::string path = input_file;
  uint64_t slash_bits;  // Unused because this path is only used for lookup.
  if (!CanonicalizePath(&path, &slash_bits, err))
    return false;
  Node* node = state_.LookupNode(path);
  if (!node)
    return false;

  Builder builder(&state_, config_, &build_log_, &disk_interface_);
  if (!builder.AddTarget(node, err))
    return false;

  if (builder.AlreadyUpToDate())
    return false;  // Not an error, but we didn't rebuild.

  if (!builder.Build(err))
    return false;

  // The manifest was only rebuilt if it is now dirty (it may have been cleaned
  // by a restat).
  if (!node->dirty()) {
    // Reset the state to prevent problems like
    // https://github.com/ninja-build/ninja/issues/874
    state_.Reset();
    return false;
  }

  return true;
}

Node* NinjaMain::CollectTarget(const char* cpath, bool source_dwim,
                               std::string* err) {
  std::string path = cpath;
  uint64_t slash_bits;
  if (!CanonicalizePath(&path, &slash_bits, err))
    return nullptr;

  // Special syntax: "foo.cc^" means "the first output of foo.cc".
  bool first_dependent = false;
  if (!path.empty() && path[path.size() - 1] == '^') {
    path.resize(path.size() - 1);
    first_dependent = true;
  }

  Node* node = state_.LookupNode(path);
  if (node) {
    if (first_dependent) {
      if (node->out_edges().empty()) {
        *err = "'" + path + "' has no out edge";
        return nullptr;
      }
      Edge* edge = node->out_edges()[0];
      if (edge->outputs_.empty()) {
        edge->Dump();
        Fatal("edge has no outputs");
      }
      node = edge->outputs_[0];
    } else if (source_dwim && !node->in_edge() && !node->out_edges().empty()) {
      Edge* edge = node->out_edges()[0];
      node = edge->outputs_[0];
    }
    return node;
  } else {
    *err =
        "unknown target '" + Node::PathDecanonicalized(path, slash_bits) + "'";
    if (path == "clean") {
      *err += ", did you mean 'ninja -t clean'?";
    } else if (path == "help") {
      *err += ", did you mean 'ninja -h'?";
    }
    return nullptr;
  }
}

Node* NinjaMain::CollectTarget(const char* cpath, std::string* err) {
  return CollectTarget(cpath, false, err);
}

bool NinjaMain::CollectTargetsFromArgs(int argc, char* argv[], bool source_dwim,
                                       std::vector<Node*>* targets,
                                       std::string* err) {
  if (argc == 0) {
    *targets = state_.DefaultNodes(err);
    return err->empty();
  }

  for (int i = 0; i < argc; ++i) {
    Node* node = CollectTarget(argv[i], source_dwim, err);
    if (node == nullptr)
      return false;
    targets->push_back(node);
  }
  return true;
}

bool NinjaMain::CollectTargetsFromArgs(int argc, char* argv[],
                                       std::vector<Node*>* targets,
                                       std::string* err) {
  return CollectTargetsFromArgs(argc, argv, false, targets, err);
}

int NinjaMain::ToolGraph(const Options* options, int argc, char* argv[]) {
  std::vector<Node*> nodes;
  std::string err;
  if (!CollectTargetsFromArgs(argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  GraphViz graph;
  graph.Start();
  for (std::vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end();
       ++n)
    graph.AddTarget(*n);
  graph.Finish();

  return 0;
}

int NinjaMain::ToolQuery(const Options* options, int argc, char* argv[]) {
  if (argc == 0) {
    Error("expected a target to query");
    return 1;
  }

  for (int i = 0; i < argc; ++i) {
    std::string err;
    Node* node = CollectTarget(argv[i], &err);
    if (!node) {
      Error("%s", err.c_str());
      return 1;
    }

    printf("%s:\n", node->path().c_str());
    if (Edge* edge = node->in_edge()) {
      printf("  input: %s\n", edge->rule_->name().c_str());
      for (int in = 0; in < (int)edge->inputs_.size(); in++) {
        const char* label = "";
        if (edge->is_implicit(in))
          label = "| ";
        else if (edge->is_order_only(in))
          label = "|| ";
        printf("    %s%s\n", label, edge->inputs_[in]->path().c_str());
      }
    }
    printf("  outputs:\n");
    for (std::vector<Edge*>::const_iterator edge = node->out_edges().begin();
         edge != node->out_edges().end(); ++edge) {
      for (std::vector<Node*>::iterator out = (*edge)->outputs_.begin();
           out != (*edge)->outputs_.end(); ++out) {
        printf("    %s\n", (*out)->path().c_str());
      }
    }
  }
  return 0;
}

#if defined(NINJA_HAVE_BROWSE)
int NinjaMain::ToolBrowse(const Options* options, int argc, char* argv[]) {
  RunBrowsePython(&state_, ninja_command_, options->input_file, argc, argv);
  // If we get here, the browse failed.
  return 1;
}
#endif  // _WIN32

#if defined(_MSC_VER)
int NinjaMain::ToolMSVC(const Options* options, int argc, char* argv[]) {
  // Reset getopt: push one argument onto the front of argv, reset optind.
  argc++;
  argv--;
  optind = 0;
  return MSVCHelperMain(argc, argv);
}
#endif

int ToolTargetsList(const std::vector<Node*>& nodes, int depth, int indent) {
  for (std::vector<Node*>::const_iterator n = nodes.begin(); n != nodes.end();
       ++n) {
    for (int i = 0; i < indent; ++i)
      printf("  ");
    const char* target = (*n)->path().c_str();
    if ((*n)->in_edge()) {
      printf("%s: %s\n", target, (*n)->in_edge()->rule_->name().c_str());
      if (depth > 1 || depth <= 0)
        ToolTargetsList((*n)->in_edge()->inputs_, depth - 1, indent + 1);
    } else {
      printf("%s\n", target);
    }
  }
  return 0;
}

int ToolTargetsSourceList(State* state) {
  for (const auto& e : state->edges_) {
    for (const auto& inps : e->inputs_) {
      if (!inps->in_edge())
        printf("%s\n", inps->path().c_str());
    }
  }
  return 0;
}

int ToolTargetsList(State* state, const std::string& rule_name) {
  std::set<std::string> rules;

  // Gather the outputs.
  for (const auto& e : state->edges_) {
    if (e->rule_->name() == rule_name) {
      for (const auto& out_node : e->outputs_) {
        rules.insert(out_node->path());
      }
    }
  }

  // Print them.
  for (std::set<std::string>::const_iterator i = rules.begin();
       i != rules.end(); ++i) {
    printf("%s\n", (*i).c_str());
  }

  return 0;
}

int ToolTargetsList(State* state) {
  for (const auto& e : state->edges_) {
    for (const auto& out_node : e->outputs_) {
      printf("%s: %s\n", out_node->path().c_str(), e->rule_->name().c_str());
    }
  }
  return 0;
}

int NinjaMain::ToolDeps(const Options* options, int argc, char** argv) {
  std::vector<Node*> nodes;
  if (argc == 0) {
    for (std::vector<Node*>::const_iterator ni = build_log_.nodes().begin();
         ni != build_log_.nodes().end(); ++ni) {
      if (build_log_.IsDepsEntryLiveFor(*ni))
        nodes.push_back(*ni);
    }
  } else {
    std::string err;
    if (!CollectTargetsFromArgs(argc, argv, &nodes, &err)) {
      Error("%s", err.c_str());
      return 1;
    }
  }

  RealDiskInterface disk_interface;
  for (std::vector<Node*>::iterator it = nodes.begin(), end = nodes.end();
       it != end; ++it) {
    BuildLog::Deps* deps = build_log_.GetDeps(*it);
    if (!deps) {
      printf("%s: deps not found\n", (*it)->path().c_str());
      continue;
    }

    std::string err;
    TimeStamp mtime = disk_interface.Stat((*it)->path(), &err);
    if (mtime == -1)
      Error("%s", err.c_str());  // Log and ignore Stat() errors;
    printf("%s: #deps %d, deps mtime %" PRId64 " (%s)\n", (*it)->path().c_str(),
           deps->node_count, deps->mtime,
           (!mtime || mtime > deps->mtime ? "STALE" : "VALID"));
    for (int i = 0; i < deps->node_count; ++i)
      printf("    %s\n", deps->nodes[i]->path().c_str());
    printf("\n");
  }

  return 0;
}

int NinjaMain::ToolTargets(const Options* options, int argc, char* argv[]) {
  int depth = 1;
  if (argc >= 1) {
    std::string mode = argv[0];
    if (mode == "rule") {
      std::string rule;
      if (argc > 1)
        rule = argv[1];
      if (rule.empty())
        return ToolTargetsSourceList(&state_);
      else
        return ToolTargetsList(&state_, rule);
    } else if (mode == "depth") {
      if (argc > 1)
        depth = atoi(argv[1]);
    } else if (mode == "all") {
      return ToolTargetsList(&state_);
    } else {
      Error("unknown target tool mode '%s'", mode.c_str());
      return 1;
    }
  }

  std::string err;
  std::vector<Node*> root_nodes = state_.RootNodes(&err);
  if (err.empty()) {
    return ToolTargetsList(root_nodes, depth, 0);
  } else {
    Error("%s", err.c_str());
    return 1;
  }
}

enum PrintCommandMode { PCM_Single, PCM_All };
void PrintCommands(Edge* edge, std::set<Edge*>* seen, PrintCommandMode mode) {
  if (!edge)
    return;
  if (!seen->insert(edge).second)
    return;

  if (mode == PCM_All) {
    for (std::vector<Node*>::iterator in = edge->inputs_.begin();
         in != edge->inputs_.end(); ++in)
      PrintCommands((*in)->in_edge(), seen, mode);
  }

  if (!edge->is_phony())
    puts(edge->EvaluateCommand().c_str());
}

int NinjaMain::ToolCommands(const Options* options, int argc, char* argv[]) {
  // The clean tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "commands".
  ++argc;
  --argv;

  PrintCommandMode mode = PCM_All;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hs"))) != -1) {
    switch (opt) {
    case 's':
      mode = PCM_Single;
      break;
    case 'h':
    default:
      printf(
          "usage: ninja -t commands [options] [targets]\n"
          "\n"
          "options:\n"
          "  -s     only print the final command to build [target], not the "
          "whole chain\n");
      return 1;
    }
  }
  argv += optind;
  argc -= optind;

  std::vector<Node*> nodes;
  std::string err;
  if (!CollectTargetsFromArgs(argc, argv, &nodes, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  std::set<Edge*> seen;
  for (std::vector<Node*>::iterator in = nodes.begin(); in != nodes.end(); ++in)
    PrintCommands((*in)->in_edge(), &seen, mode);

  return 0;
}

int NinjaMain::ToolClean(const Options* options, int argc, char* argv[]) {
  // The clean tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "clean".
  argc++;
  argv--;

  bool generator = false;
  bool clean_rules = false;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hgr"))) != -1) {
    switch (opt) {
    case 'g':
      generator = true;
      break;
    case 'r':
      clean_rules = true;
      break;
    case 'h':
    default:
      printf(
          "usage: ninja -t clean [options] [targets]\n"
          "\n"
          "options:\n"
          "  -g     also clean files marked as ninja generator output\n"
          "  -r     interpret targets as a list of rules to clean instead\n");
      return 1;
    }
  }
  argv += optind;
  argc -= optind;

  if (clean_rules && argc == 0) {
    Error("expected a rule to clean");
    return 1;
  }

  Cleaner cleaner(&state_, config_);
  if (argc >= 1) {
    if (clean_rules)
      return cleaner.CleanRules(argc, argv);
    else
      return cleaner.CleanTargets(argc, argv);
  } else {
    return cleaner.CleanAll(generator);
  }
}

void EncodeJSONString(const char* str) {
  while (*str) {
    if (*str == '"' || *str == '\\')
      putchar('\\');
    putchar(*str);
    str++;
  }
}

enum EvaluateCommandMode { ECM_NORMAL, ECM_EXPAND_RSPFILE };
std::string EvaluateCommandWithRspfile(Edge* edge, EvaluateCommandMode mode) {
  std::string command = edge->EvaluateCommand();
  if (mode == ECM_NORMAL)
    return command;

  std::string rspfile = edge->GetUnescapedRspfile();
  if (rspfile.empty())
    return command;

  size_t index = command.find(rspfile);
  if (index == 0 || index == std::string::npos || command[index - 1] != '@')
    return command;

  std::string rspfile_content = edge->GetBinding("rspfile_content");
  size_t newline_index = 0;
  while ((newline_index = rspfile_content.find('\n', newline_index)) !=
         std::string::npos) {
    rspfile_content.replace(newline_index, 1, 1, ' ');
    ++newline_index;
  }
  command.replace(index - 1, rspfile.length() + 1, rspfile_content);
  return command;
}

int NinjaMain::ToolCompilationDatabase(const Options* options, int argc,
                                       char* argv[]) {
  // The compdb tool uses getopt, and expects argv[0] to contain the name of
  // the tool, i.e. "compdb".
  argc++;
  argv--;

  EvaluateCommandMode eval_mode = ECM_NORMAL;

  optind = 1;
  int opt;
  while ((opt = getopt(argc, argv, const_cast<char*>("hx"))) != -1) {
    switch (opt) {
    case 'x':
      eval_mode = ECM_EXPAND_RSPFILE;
      break;

    case 'h':
    default:
      printf(
          "usage: ninja -t compdb [options] [rules]\n"
          "\n"
          "options:\n"
          "  -x     expand @rspfile style response file invocations\n");
      return 1;
    }
  }
  argv += optind;
  argc -= optind;

  bool first = true;
  std::string err;
  std::string cwd = GetCwd(&err);

  if (cwd.empty()) {
    Error("cannot determine working directory: %s", err.c_str());
    return 1;
  }

  putchar('[');
  for (const auto& e : state_.edges_) {
    if (e->inputs_.empty())
      continue;
    for (int i = 0; i != argc; ++i) {
      if (e->rule_->name() == argv[i]) {
        if (!first)
          putchar(',');

        printf("\n  {\n    \"directory\": \"");
        EncodeJSONString(&cwd[0]);
        printf("\",\n    \"command\": \"");
        EncodeJSONString(
            EvaluateCommandWithRspfile(e.get(), eval_mode).c_str());
        printf("\",\n    \"file\": \"");
        EncodeJSONString(e->inputs_[0]->path().c_str());
        printf("\",\n    \"output\": \"");
        EncodeJSONString(e->outputs_[0]->path().c_str());
        printf("\"\n  }");

        first = false;
      }
    }
  }

  puts("\n]");
  return 0;
}

int NinjaMain::ToolRecompact(const Options* options, int argc, char* argv[]) {
  if (!EnsureBuildDirExists())
    return 1;

  if (!OpenBuildLog(/*recompact_only=*/true))
    return 1;

  return 0;
}

int NinjaMain::ToolUrtle(const Options* options, int argc, char** argv) {
  // RLE encoded.
  const char* urtle =
      " 13 ,3;2!2;\n8 ,;<11!;\n5 `'<10!(2`'2!\n11 ,6;, `\\. `\\9 .,c13$ec,.\n6 "
      ",2;11!>; `. ,;!2> .e8$2\".2 \"?7$e.\n <:<8!'` 2.3,.2` ,3!' ;,(?7\";2!2'<"
      "; `?6$PF ,;,\n2 `'4!8;<!3'`2 3! ;,`'2`2'3!;4!`2.`!;2 3,2 .<!2'`).\n5 3`5"
      "'2`9 `!2 `4!><3;5! J2$b,`!>;2!:2!`,d?b`!>\n26 `'-;,(<9!> $F3 )3.:!.2 d\""
      "2 ) !>\n30 7`2'<3!- \"=-='5 .2 `2-=\",!>\n25 .ze9$er2 .,cd16$bc.'\n22 .e"
      "14$,26$.\n21 z45$c .\n20 J50$c\n20 14$P\"`?34$b\n20 14$ dbc `2\"?22$?7$c"
      "\n20 ?18$c.6 4\"8?4\" c8$P\n9 .2,.8 \"20$c.3 ._14 J9$\n .2,2c9$bec,.2 `?"
      "21$c.3`4%,3%,3 c8$P\"\n22$c2 2\"?21$bc2,.2` .2,c7$P2\",cb\n23$b bc,.2\"2"
      "?14$2F2\"5?2\",J5$P\" ,zd3$\n24$ ?$3?%3 `2\"2?12$bcucd3$P3\"2 2=7$\n23$P"
      "\" ,3;<5!>2;,. `4\"6?2\"2 ,9;, `\"?2$\n";
  int count = 0;
  for (const char* p = urtle; *p; p++) {
    if ('0' <= *p && *p <= '9') {
      count = count * 10 + *p - '0';
    } else {
      for (int i = 0; i < std::max(count, 1); ++i)
        printf("%c", *p);
      count = 0;
    }
  }
  return 0;
}

const Tool* ChooseTool(const std::string& tool_name) {
  static const Tool kTools[] = {
#if defined(NINJA_HAVE_BROWSE)
    { "browse", "browse dependency graph in a web browser",
      Tool::RUN_AFTER_LOAD, &NinjaMain::ToolBrowse },
#endif
#if defined(_MSC_VER)
    { "msvc", "build helper for MSVC cl.exe (EXPERIMENTAL)",
      Tool::RUN_AFTER_FLAGS, &NinjaMain::ToolMSVC },
#endif
    { "clean", "clean built files", Tool::RUN_AFTER_LOAD,
      &NinjaMain::ToolClean },
    { "commands", "list all commands required to rebuild given targets",
      Tool::RUN_AFTER_LOAD, &NinjaMain::ToolCommands },
    { "deps", "show dependencies stored in the deps log", Tool::RUN_AFTER_LOGS,
      &NinjaMain::ToolDeps },
    { "graph", "output graphviz dot file for targets", Tool::RUN_AFTER_LOAD,
      &NinjaMain::ToolGraph },
    { "query", "show inputs/outputs for a path", Tool::RUN_AFTER_LOGS,
      &NinjaMain::ToolQuery },
    { "targets", "list targets by their rule or depth in the DAG",
      Tool::RUN_AFTER_LOAD, &NinjaMain::ToolTargets },
    { "compdb", "dump JSON compilation database to stdout",
      Tool::RUN_AFTER_LOAD, &NinjaMain::ToolCompilationDatabase },
    { "recompact", "recompacts ninja-internal data structures",
      Tool::RUN_AFTER_LOAD, &NinjaMain::ToolRecompact },
    { "urtle", nullptr, Tool::RUN_AFTER_FLAGS, &NinjaMain::ToolUrtle },
    { nullptr, nullptr, Tool::RUN_AFTER_FLAGS, nullptr }
  };

  if (tool_name == "list") {
    printf("ninja subtools:\n");
    for (const Tool* tool = &kTools[0]; tool->name; ++tool) {
      if (tool->desc)
        printf("%10s  %s\n", tool->name, tool->desc);
    }
    return nullptr;
  }

  for (const Tool* tool = &kTools[0]; tool->name; ++tool) {
    if (tool->name == tool_name)
      return tool;
  }

  Fatal("unknown tool '%s'", tool_name.c_str());
  return nullptr;  // Not reached.
}

bool DebugEnable(const std::string& name) {
  if (name == "list") {
    printf(
        "debugging modes:\n"
        "  stats        print operation counts/timing info\n"
        "  explain      explain what caused a command to execute\n"
        "  keepdepfile  don't delete depfiles after they're read by ninja\n"
        "  keeprsp      don't delete @response files on success\n"
#ifdef _WIN32
        "  nostatcache  don't batch stat() calls per directory and cache them\n"
#endif
        "multiple modes can be enabled via -d FOO -d BAR\n");
    return false;
  } else if (name == "stats") {
    g_metrics = new Metrics;
    return true;
  } else if (name == "explain") {
    g_explaining = true;
    return true;
  } else if (name == "keepdepfile") {
    g_keep_depfile = true;
    return true;
  } else if (name == "keeprsp") {
    g_keep_rsp = true;
    return true;
  } else {
    Error("unknown debug setting '%s'", name.c_str());
    return false;
  }
}

bool WarningEnable(const std::string& name, Options* options) {
  if (name == "list") {
    printf(
        "warning flags:\n"
        "  dupbuild={err,warn}  multiple build lines for one target\n"
        "  phonycycle={err,warn}  phony build statement references itself\n");
    return false;
  } else if (name == "dupbuild=err") {
    options->dupe_edges_should_err = true;
    return true;
  } else if (name == "dupbuild=warn") {
    options->dupe_edges_should_err = false;
    return true;
  } else if (name == "phonycycle=err") {
    options->phony_cycle_should_err = true;
    return true;
  } else if (name == "phonycycle=warn") {
    options->phony_cycle_should_err = false;
    return true;
  } else {
    Error("unknown warning flag '%s'", name.c_str());
    return false;
  }
}

bool NinjaMain::OpenBuildLog(bool recompact_only) {
  std::string log_path = BuildLog::kFilename;
  if (!build_dir_.empty())
    log_path = build_dir_ + "/" + log_path;

  std::string err;
  if (!build_log_.Load(log_path, &state_, &err)) {
    Error("loading build log %s: %s", log_path.c_str(), err.c_str());
    return false;
  }
  if (!err.empty()) {
    // Hack: Load() can return a warning via err by returning true.
    Warning("%s", err.c_str());
    err.clear();
  }

  if (recompact_only) {
    bool success = build_log_.Recompact(log_path, *this, &err);
    if (!success)
      Error("failed recompaction: %s", err.c_str());
    return success;
  }

  if (!config_.dry_run) {
    if (!build_log_.OpenForWrite(log_path, *this, &err)) {
      Error("opening build log: %s", err.c_str());
      return false;
    }
  }

  return true;
}

void NinjaMain::DumpMetrics() {
  g_metrics->Report();

  printf("\n");
  int count = (int)state_.paths_.size();
  int buckets = (int)state_.paths_.bucket_count();
  printf("path->node hash load %.2f (%d entries / %d buckets)\n",
         count / (double)buckets, count, buckets);
}

bool NinjaMain::EnsureBuildDirExists() {
  build_dir_ = state_.bindings_->LookupVariable("builddir");
  if (!build_dir_.empty() && !config_.dry_run) {
    if (!disk_interface_.MakeDirs(build_dir_ + "/.") && errno != EEXIST) {
      Error("creating build directory %s: %s", build_dir_.c_str(),
            strerror(errno));
      return false;
    }
  }
  return true;
}

int NinjaMain::RunBuild(int argc, char** argv, bool source_dwim) {
  std::string err;
  std::vector<Node*> targets;
  if (!CollectTargetsFromArgs(argc, argv, source_dwim, &targets, &err)) {
    Error("%s", err.c_str());
    return 1;
  }

  Builder builder(&state_, config_, &build_log_, &disk_interface_);
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!builder.AddTarget(targets[i], &err)) {
      if (!err.empty()) {
        Error("%s", err.c_str());
        return 1;
      } else {
        // Added a target that is already up-to-date; not really
        // an error.
      }
    }
  }

  if (builder.AlreadyUpToDate()) {
    printf("ninja: no work to do.\n");
    return 0;
  }

  if (!builder.Build(&err)) {
    printf("ninja: build stopped: %s.\n", err.c_str());
    if (err.find("interrupted by user") != std::string::npos) {
      return 2;
    }
    return 1;
  }

  return 0;
}

#ifdef _MSC_VER
/// This handler processes fatal crashes that you can't catch
/// Test example: C++ exception in a stack-unwind-block
/// Real-world example: ninja launched a compiler to process a tricky
/// C++ input file. The compiler got itself into a state where it
/// generated 3 GB of output and caused ninja to crash.
void TerminateHandler() {
  CreateWin32MiniDump(nullptr);
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

int guarded_main(MainFunction real_main, int argc, char** argv) {
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
  return 0;
}
}  // namespace ninja
