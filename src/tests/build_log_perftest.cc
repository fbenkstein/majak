// Copyright 2012 Google Inc. All Rights Reserved.
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

#include <benchmark/benchmark.h>
#include <stdio.h>
#include <stdlib.h>

#include <ninja/build_log.h>
#include <ninja/filesystem.h>
#include <ninja/graph.h>
#include <ninja/manifest_parser.h>
#include <ninja/metrics.h>
#include <ninja/state.h>
#include <ninja/util.h>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace ninja;

namespace {
double now() {
  return std::chrono::duration_cast<std::chrono::duration<double>>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

const char kTestFilename[] = "BuildLogPerfTest-tempfile";

struct NoDeadPaths : public BuildLogUser {
  virtual bool IsPathDead(std::string_view) const { return false; }
};

bool WriteTestData(std::string* err) {
  BuildLog log;

  NoDeadPaths no_dead_paths;
  if (!log.OpenForWrite(kTestFilename, no_dead_paths, err))
    return false;

  /*
  A histogram of command lengths in chromium. For example, 407 builds,
  1.4% of all builds, had commands longer than 32 bytes but shorter than 64.
       32    407   1.4%
       64    183   0.6%
      128   1461   5.1%
      256    791   2.8%
      512   1314   4.6%
     1024   6114  21.3%
     2048  11759  41.0%
     4096   2056   7.2%
     8192   4567  15.9%
    16384     13   0.0%
    32768      4   0.0%
    65536      5   0.0%
  The average command length is 4.1 kB and there were 28674 commands in total,
  which makes for a total log size of ~120 MB (also counting output filenames).

  Based on this, write 30000 many 4 kB long command lines.
  */

  // ManifestParser is the only object allowed to create Rules.
  const size_t kRuleSize = 4000;
  std::string long_rule_command = "gcc ";
  for (int i = 0; long_rule_command.size() < kRuleSize; ++i) {
    char buf[80];
    sprintf(buf, "-I../../and/arbitrary/but/fairly/long/path/suffixed/%d ", i);
    long_rule_command += buf;
  }
  long_rule_command += "$in -o $out\n";

  State state;
  ManifestParser parser(&state, nullptr);
  if (!parser.ParseTest("rule cxx\n  command = " + long_rule_command, err))
    return false;

  // Create build edges. Using ManifestParser is as fast as using the State api
  // for edge creation, so just use that.
  const int kNumCommands = 30000;
  std::string build_rules;
  for (int i = 0; i < kNumCommands; ++i) {
    char buf[80];
    sprintf(buf, "build input%d.o: cxx input%d.cc\n", i, i);
    build_rules += buf;
  }

  if (!parser.ParseTest(build_rules, err))
    return false;

  for (int i = 0; i < kNumCommands; ++i) {
    log.RecordCommand(state.edges_[i].get(),
                      /*start_time=*/100 * i,
                      /*end_time=*/100 * i + 1,
                      /*mtime=*/0);
  }

  return true;
}
}  // anonymous namespace

void BM_BuildLogLoad(benchmark::State& state) {
  std::string err;
  fs::error_code ec;
  fs::remove(kTestFilename, ec);
  State ninja_state;

  if (!WriteTestData(&err)) {
    state.SkipWithError(("Failed to write test data: " + err).c_str());
    return;
  }

  {
    // Read once to warm up disk cache.
    BuildLog log;
    if (!log.Load(kTestFilename, &ninja_state, &err)) {
      state.SkipWithError(("Failed to load test data: " + err).c_str());
      return;
    }
  }

  for (auto _ : state) {
    auto start = now();
    BuildLog log;
    if (!log.Load(kTestFilename, &ninja_state, &err)) {
      state.SkipWithError(("Failed to load test data: " + err).c_str());
      return;
    }
    state.SetIterationTime(now() - start);
  }

  fs::remove(kTestFilename, ec);
}
BENCHMARK(BM_BuildLogLoad)->Unit(benchmark::kMillisecond)->UseManualTime();

BENCHMARK_MAIN();
