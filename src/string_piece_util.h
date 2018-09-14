// Copyright 2017 Google Inc. All Rights Reserved.
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

#ifndef NINJA_STRINGPIECE_UTIL_H_
#define NINJA_STRINGPIECE_UTIL_H_

#include <string>
#include <string_view>
#include <vector>

std::vector<std::string_view> SplitStringView(std::string_view input, char sep);

std::string JoinStringView(const std::vector<std::string_view>& list, char sep);

inline char ToLowerASCII(char c) {
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

bool EqualsCaseInsensitiveASCII(std::string_view a, std::string_view b);

#endif  // NINJA_STRINGPIECE_UTIL_H_
