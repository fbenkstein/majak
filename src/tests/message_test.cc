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

#include "message.h"
#include "debug_flags.h"

#include <cstdlib>

#include <gtest/gtest.h>

TEST(MessageTest, Fatal) {
  auto f = [] { ninja::Fatal("the %s broke", "thing"); };

  ASSERT_EXIT(f(), ::testing::ExitedWithCode(1),
              "^ninja: fatal: the thing broke\n$");
}

TEST(MessageTest, Error) {
  auto f = [] {
    ninja::Error("the %s broke", "thing");
    std::exit(13);
  };
  ASSERT_EXIT(f(), ::testing::ExitedWithCode(13),
              "^ninja: error: the thing broke\n$");
}

TEST(MessageTest, Warning) {
  auto f = [] {
    ninja::Warning("the %s broke", "thing");
    std::exit(13);
  };
  ASSERT_EXIT(f(), ::testing::ExitedWithCode(13),
              "^ninja: warning: the thing broke\n$");
}

TEST(MessageTest, EXPLAIN_OFF) {
  auto f = [] {
    ninja::g_explaining = false;
    fputs("before", stderr);
    fflush(stderr);
    ninja::EXPLAIN("the %s happened", "thing");
    fputs("after", stderr);
    fflush(stderr);
    std::exit(13);
  };
  ASSERT_EXIT(f(), ::testing::ExitedWithCode(13), "^beforeafter$");
}

TEST(MessageTest, EXPLAIN_ON) {
  auto f = [] {
    ninja::g_explaining = true;
    fputs("before", stderr);
    fflush(stderr);
    ninja::EXPLAIN("the %s happened", "thing");
    fputs("after", stderr);
    fflush(stderr);
    std::exit(13);
  };
  ASSERT_EXIT(f(), ::testing::ExitedWithCode(13),
              "ninja explain: the thing happened");
}
