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

#include "filesystem.h"
#include "metrics.h"
#include "util.h"

namespace fs = ninja::fs;

namespace {

constexpr char kPath[] =
    "../../third_party/WebKit/Source/WebCore/"
    "platform/leveldb/LevelDBWriteBatch.cpp";
}  // namespace

/// Keep comparable with legacy ninja.
static void BM_CanonicalizePathLegacy(benchmark::State& state) {
  std::string err;

  constexpr int kNumRepetitions = 2000000;
  uint64_t slash_bits;
  char buf[200];
  size_t len = strlen(kPath);
  strcpy(buf, kPath);

  for (auto _ : state) {
    for (int i = 0; i < kNumRepetitions; ++i) {
      CanonicalizePath(buf, &len, &slash_bits, &err);
    }
  }
}
BENCHMARK(BM_CanonicalizePathLegacy)->Unit(benchmark::kMillisecond);

static void BM_CanonicalizePath(benchmark::State& state) {
  std::string err;

  uint64_t slash_bits;
  char buf[200];
  size_t len = strlen(kPath);
  strcpy(buf, kPath);

  for (auto _ : state) {
    CanonicalizePath(buf, &len, &slash_bits, &err);
  }
}
BENCHMARK(BM_CanonicalizePath);

static void BM_EmptyPath(benchmark::State& state) {
  for (auto _ : state) {
    fs::path p;
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_EmptyPath);

static void BM_EmptyPathPauseResume(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    state.ResumeTiming();
    fs::path p;
    benchmark::DoNotOptimize(p);
  }
}
BENCHMARK(BM_EmptyPathPauseResume);

static void BM_StringToPath(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        std::string s = kPath;
        state.ResumeTiming();
        fs::path p(std::move(s));
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_StringToPath);

static void BM_PathToPath(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        fs::path s = kPath;
        state.ResumeTiming();
        fs::path p(std::move(s));
        benchmark::DoNotOptimize(p);
    }
}
BENCHMARK(BM_PathToPath);

BENCHMARK_MAIN();
