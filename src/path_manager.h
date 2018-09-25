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

#ifndef NINJA_PATH_MANAGER_H_
#define NINJA_PATH_MANAGER_H_

#include "filesystem.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ninja {
struct PathId {
  uint64_t value;
};

fs::path NormalizePath(const fs::path& base_dir, const fs::path& path);

class PathHash {
 public:
  explicit PathHash(fs::path base_dir) : base_dir_(base_dir){};
  size_t operator()(const std::string_view& s) const;

 private:
  fs::path base_dir_;
};

class PathEqual {
 public:
  explicit PathEqual(fs::path base_dir) : base_dir_(base_dir){};
  bool operator()(const std::string_view& s1, const std::string_view& s2) const;

 private:
  fs::path base_dir_;
};

class PathManager {
 public:
  explicit PathManager(std::string_view base_dir);

  // Look up the ID of a path.
  std::optional<PathId> LookupId(std::string_view path) const;

  // Get the ID for a path. Create a new ID if none doesn't exist yet.
  PathId GetId(std::string_view path);

  std::string GetPath(PathId id);

 private:
  std::vector<std::unique_ptr<std::string>> paths_;
  std::unordered_map<std::string_view, PathId, PathHash, PathEqual> path_index_;
};

inline bool operator==(const PathId& id1, const PathId& id2) {
  return id1.value == id2.value;
}

inline bool operator!=(const PathId& id1, const PathId& id2) {
  return !(id1 == id2);
}
}  // namespace ninja

#endif  // NINJA_PATH_MANAGER_H_
