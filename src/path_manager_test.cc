// Copyright 2018 Frank Benkstein
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

#include "path_manager.h"

#include <gtest/gtest.h>

#include <functional>

using namespace ninja;

namespace {

enum {
  ExpectEqual,
  ExpectUnequal,
};

std::ostream& operator<<(std::ostream& os, decltype(ExpectEqual) e) {
  switch (e) {
  case ExpectEqual:
    return os << "ExpectEqual";
  case ExpectUnequal:
    return os << "ExpectUnequal";
  default:
    return os << "Unknown";
  };
}

struct PathTest
    : ::testing::TestWithParam<std::tuple<std::string, std::string, std::string,
                                          decltype(ExpectEqual)>> {};

TEST_P(PathTest, Equals) {
  auto[base_dir, p1, p2, expect_equal] = GetParam();
  const PathEqual path_equal{ base_dir };
  const auto path_not_equal = std::not_fn(path_equal);

  switch (expect_equal) {
  case ExpectEqual:
    ASSERT_PRED2(path_equal, p1, p2);
    ASSERT_PRED2(path_equal, p2, p1);
    break;
  case ExpectUnequal:
    ASSERT_PRED2(path_not_equal, p1, p2);
    ASSERT_PRED2(path_not_equal, p2, p1);
    break;
  }
}

TEST_P(PathTest, Hash) {
  auto[base_dir, p1, p2, expect_equal] = GetParam();
  const PathHash path_hash{ base_dir };
  const auto path_hash_equal = [path_hash](auto p1, auto p2) {
    return path_hash(p1) == path_hash(p2);
  };
  const auto path_hash_not_equal = std::not_fn(path_hash_equal);

  switch (expect_equal) {
  case ExpectEqual:
    EXPECT_PRED2(path_hash_equal, p1, p2);
    break;
  case ExpectUnequal:
    EXPECT_PRED2(path_hash_not_equal, p1, p2);
    break;
  }
}

namespace {
const PathTest::ParamType path_test_values[] = {
  { ".", "", "", ExpectEqual },
  { ".", "a", "", ExpectUnequal },
  { ".", "a", "a", ExpectEqual },
  { ".", "a", "a", ExpectEqual },
  { ".", "a/b", "a/b", ExpectEqual },
  { ".", "a/b/c", "a/b/c", ExpectEqual },
  { ".", "a/b/c", "a/b/d", ExpectUnequal },
  // ??? { ".", "a/b/", "a/b", ExpectUnequal },
  { ".", "a/b", "a/./b", ExpectEqual },
  { ".", "a/b", "a//b", ExpectEqual },
  { ".", "a/b", "a///////b", ExpectEqual },
  { ".", "a/b/../c", "a/c", ExpectEqual },
  { "/a", "b", "/a/b", ExpectEqual },
  { "/a/b", "../c", "/a/c", ExpectEqual },
  { "/a/b", "./c", "../b/c", ExpectEqual },
};
}

INSTANTIATE_TEST_CASE_P(PathTest, PathTest,
                        ::testing::ValuesIn(path_test_values));

}  // namespace
