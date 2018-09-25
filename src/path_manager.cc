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

#include "filesystem.h"

#include <algorithm>
#include <cstring>

namespace ninja {

namespace {
static const uint32_t kSeed = 0xDECAFBAD;

// MurmurHash2, by Austin Appleby
uint32_t MurmurHash2(const void* key, size_t len, uint32_t seed = kSeed) {
  const uint32_t m = 0x5bd1e995;
  const int r = 24;
  uint32_t h = seed ^ len;
  const unsigned char* data = (const unsigned char*)key;
  while (len >= 4) {
    uint32_t k;
    memcpy(&k, data, sizeof k);
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
    data += 4;
    len -= 4;
  }
  switch (len) {
  case 3:
    h ^= data[2] << 16;
  case 2:
    h ^= data[1] << 8;
  case 1:
    h ^= data[0];
    h *= m;
  };
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

uint32_t MurmurHash2(std::string_view s, uint32_t seed = kSeed) {
  return MurmurHash2(s.data(), s.size(), seed);
}

uint32_t MurmurHash2(const fs::path& p, uint32_t seed = kSeed) {
  return MurmurHash2(
      p.native().data(),
      p.native().size() * sizeof(fs::path::string_type::value_type), seed);
}
}  // anonymous namespace

fs::path NormalizePath(const fs::path& base_dir, const fs::path& path) {
  std::vector<fs::path> result_components;

  if (path.is_relative()) {
    result_components.insert(result_components.end(), base_dir.begin(),
                             base_dir.end());
  }

  for (const auto& c : path) {
    if (c == ".." && !result_components.empty()) {
      result_components.pop_back();
      continue;
    }

    if (c == ".") {
      continue;
    }

    result_components.push_back(c);
  }

  fs::path result;

  for (const auto& c : result_components) {
    result /= c;
  }

  return result;
}

size_t PathHash::operator()(const std::string_view& s) const {
  fs::path p = NormalizePath(base_dir_, s);
  auto it = p.begin();
  auto itend = p.end();

  if (it == itend) {
    return MurmurHash2(s);
  }

  uint32_t result = MurmurHash2(*it);

  for (++it; it != itend; ++it) {
    result = MurmurHash2(std::string_view{}, result);
    result = MurmurHash2(*it, result);
  }

  return result;
}

bool PathEqual::operator()(const std::string_view& s1,
                           const std::string_view& s2) const {
  fs::path p1 = NormalizePath(base_dir_, s1);
  fs::path p2 = NormalizePath(base_dir_, s2);
  return std::equal(p1.begin(), p1.end(), p2.begin(), p2.end());
}

PathManager::PathManager(std::string_view base_dir)
    : path_index_{ 1024, PathHash{ base_dir }, PathEqual{ base_dir } } {}

std::optional<PathId> PathManager::LookupId(std::string_view path) const {
  auto it = path_index_.find(path);
  return it != path_index_.end() ? std::make_optional(it->second)
                                 : std::nullopt;
}

PathId PathManager::GetId(std::string_view path) {
  if (auto id = LookupId(path)) {
    return *id;
  }

  auto id = PathId{ paths_.size() };
  paths_.emplace_back(new std::string(path));
  path_index_.insert(std::make_pair(std::string_view(*paths_.back()), id));
  return id;
}

std::string PathManager::GetPath(PathId id) {
  return *paths_[id.value];
}
}  // namespace ninja
