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

#include "includes_normalize.h"

#include "string_piece_util.h"

#include <ninja/util.h>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string_view>

#include <windows.h>

namespace ninja {

namespace {

// Return true if paths a and b are on the same windows drive.
// Return false if this funcation cannot check
// whether or not on the same windows drive.
bool SameDriveFast(std::string_view a, std::string_view b) {
  if (a.size() < 3 || b.size() < 3) {
    return false;
  }

  if (!islatinalpha(a[0]) || !islatinalpha(b[0])) {
    return false;
  }

  if (ToLowerASCII(a[0]) != ToLowerASCII(b[0])) {
    return false;
  }

  if (a[1] != ':' || b[1] != ':') {
    return false;
  }

  return IsPathSeparator(a[2]) && IsPathSeparator(b[2]);
}

// Return true if paths a and b are on the same Windows drive.
bool SameDrive(std::string_view a, std::string_view b) {
  if (SameDriveFast(a, b)) {
    return true;
  }

  char a_absolute[_MAX_PATH];
  char b_absolute[_MAX_PATH];
  GetFullPathNameA(std::string(a).c_str(), sizeof(a_absolute), a_absolute,
                   nullptr);
  GetFullPathNameA(std::string(b).c_str(), sizeof(b_absolute), b_absolute,
                   nullptr);
  char a_drive[_MAX_DIR];
  char b_drive[_MAX_DIR];
  _splitpath(a_absolute, a_drive, nullptr, nullptr, nullptr);
  _splitpath(b_absolute, b_drive, nullptr, nullptr, nullptr);
  return _stricmp(a_drive, b_drive) == 0;
}

// Check path |s| is FullPath style returned by GetFullPathName.
// This ignores difference of path separator.
// This is used not to call very slow GetFullPathName API.
bool IsFullPathName(std::string_view s) {
  if (s.size() < 3 || !islatinalpha(s[0]) || s[1] != ':' ||
      !IsPathSeparator(s[2])) {
    return false;
  }

  // Check "." or ".." is contained in path.
  for (size_t i = 2; i < s.size(); ++i) {
    if (!IsPathSeparator(s[i])) {
      continue;
    }

    // Check ".".
    if (i + 1 < s.size() && s[i + 1] == '.' &&
        (i + 2 >= s.size() || IsPathSeparator(s[i + 2]))) {
      return false;
    }

    // Check "..".
    if (i + 2 < s.size() && s[i + 1] == '.' && s[i + 2] == '.' &&
        (i + 3 >= s.size() || IsPathSeparator(s[i + 3]))) {
      return false;
    }
  }

  return true;
}

}  // anonymous namespace

IncludesNormalize::IncludesNormalize(const std::string& relative_to) {
  relative_to_ = AbsPath(relative_to);
  split_relative_to_ = SplitStringView(relative_to_, '/');
}

std::string IncludesNormalize::AbsPath(std::string_view s) {
  if (IsFullPathName(s)) {
    std::string result = std::string(s);
    for (size_t i = 0; i < result.size(); ++i) {
      if (result[i] == '\\') {
        result[i] = '/';
      }
    }
    return result;
  }

  char result[_MAX_PATH];
  GetFullPathNameA(std::string(s).c_str(), sizeof(result), result, nullptr);
  for (char* c = result; *c; ++c)
    if (*c == '\\')
      *c = '/';
  return result;
}

std::string IncludesNormalize::Relativize(
    std::string_view path, const std::vector<std::string_view>& start_list) {
  std::string abs_path = AbsPath(path);
  std::vector<std::string_view> path_list = SplitStringView(abs_path, '/');
  int i;
  for (i = 0;
       i < static_cast<int>(std::min(start_list.size(), path_list.size()));
       ++i) {
    if (!EqualsCaseInsensitiveASCII(start_list[i], path_list[i])) {
      break;
    }
  }

  std::vector<std::string_view> rel_list;
  rel_list.reserve(start_list.size() - i + path_list.size() - i);
  for (int j = 0; j < static_cast<int>(start_list.size() - i); ++j)
    rel_list.push_back("..");
  for (int j = i; j < static_cast<int>(path_list.size()); ++j)
    rel_list.push_back(path_list[j]);
  if (rel_list.size() == 0)
    return ".";
  return JoinStringView(rel_list, '/');
}

bool IncludesNormalize::Normalize(const std::string& input, std::string* result,
                                  std::string* err) const {
  char copy[_MAX_PATH + 1];
  size_t len = input.size();
  if (len > _MAX_PATH) {
    *err = "path too long";
    return false;
  }
  strncpy(copy, input.c_str(), input.size() + 1);
  uint64_t slash_bits;
  if (!CanonicalizePath(copy, &len, &slash_bits, err))
    return false;
  std::string_view partially_fixed(copy, len);
  std::string abs_input = AbsPath(partially_fixed);

  if (!SameDrive(abs_input, relative_to_)) {
    *result = std::string(partially_fixed);
    return true;
  }
  *result = Relativize(abs_input, split_relative_to_);
  return true;
}

}  // namespace ninja
