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

#include <stdio.h>
#include <string.h>
#include <benchmark/benchmark.h>

#include "metrics.h"
#include "util.h"

const char kPath[] =
    "../../third_party/WebKit/Source/WebCore/"
    "platform/leveldb/LevelDBWriteBatch.cpp";

static void BM_CanonicalizePath(benchmark::State& state)
{
    std::string err;

    const int kNumRepetitions = 2000000;
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
BENCHMARK(BM_CanonicalizePath);

BENCHMARK_MAIN();
