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
#include <string.h>
#include <iostream>
#include <string>

#include "filesystem.h"
#include "metrics.h"
#include "util.h"

using namespace ninja;

namespace {

constexpr const char* kPaths[] = {
  "../../third_party/WebKit/Source/WebCore/platform/leveldb/"
  "LevelDBWriteBatch.cpp",
  "/usr/lib/gcc/x86_64-linux-gnu/7/../../../x86_64-linux-gnu",
};

double now() {
  return std::chrono::duration_cast<std::chrono::duration<double>>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}
}  // namespace

/// Keep comparable with legacy ninja.
static void BM_CanonicalizePathLegacy(benchmark::State& state) {
  std::string err;

  constexpr int kNumRepetitions = 2000000;
  uint64_t slash_bits;
  std::string s(kPaths[state.range(0)]);
  char* buf = s.data();
  size_t len = s.size();

  for (auto _ : state) {
    auto start = now();
    for (int i = 0; i < kNumRepetitions; ++i) {
      CanonicalizePath(buf, &len, &slash_bits, &err);
    }
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_CanonicalizePathLegacy)
    ->Unit(benchmark::kMillisecond)
    ->UseManualTime()
    ->Arg(0)
    ->Arg(1);

static void BM_CanonicalizePath(benchmark::State& state) {
  std::string err;

  uint64_t slash_bits;
  std::string s(kPaths[state.range(0)]);
  char* buf = s.data();
  size_t len = s.size();

  for (auto _ : state) {
    auto start = now();
    CanonicalizePath(buf, &len, &slash_bits, &err);
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_CanonicalizePath)->UseManualTime()->Arg(0)->Arg(1);

static void BM_EmptyPath(benchmark::State& state) {
  for (auto _ : state) {
    auto start = now();
    fs::path p;
    benchmark::DoNotOptimize(p);
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_EmptyPath)->UseManualTime();

static void BM_StringToPath(benchmark::State& state) {
  for (auto _ : state) {
    std::string s(kPaths[0]);
    auto start = now();
    fs::path p(std::move(s));
    benchmark::DoNotOptimize(p);
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_StringToPath)->UseManualTime();

static void BM_PathToPath(benchmark::State& state) {
  for (auto _ : state) {
    fs::path s(kPaths[0]);
    auto start = now();
    fs::path p(std::move(s));
    benchmark::DoNotOptimize(p);
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_PathToPath)->UseManualTime();

static void BM_PathAssignToPath(benchmark::State& state) {
  for (auto _ : state) {
    fs::path s(kPaths[0]);
    auto start = now();
    fs::path p;
    benchmark::DoNotOptimize(p);
    p = std::move(s);
    benchmark::DoNotOptimize(p);
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_PathAssignToPath)->UseManualTime();

static void BM_Baseline(benchmark::State& state) {
  for (auto _ : state) {
    auto start = now();
    benchmark::DoNotOptimize(start);
    state.SetIterationTime(now() - start);
  }
}
BENCHMARK(BM_Baseline)->UseManualTime();

BENCHMARK_MAIN();
