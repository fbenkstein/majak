// Copyright 2011 Google Inc. All Rights Reserved.
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

#include "test.h"

#include <algorithm>
#include <random>

#include <stdlib.h>

#include <ninja/build_log.h>
#include <ninja/graph.h>
#include <ninja/manifest_parser.h>
#include <ninja/util.h>

using namespace ninja;

namespace {
std::string MakeTempDir(std::string_view prefix, std::string* err) {
  constexpr size_t tries = 100;
  constexpr size_t suffix_length = 6;
  std::random_device rd;
  std::string name = std::string(prefix) + "-";
  size_t suffix_start = name.size();
  name.resize(suffix_start + suffix_length, '0');

  for (size_t i = 0; i < tries; ++i) {
    std::generate_n(name.begin() + suffix_start, suffix_length, [&rd] {
      return static_cast<char>(
          std::uniform_int_distribution<int>('0', '9')(rd));
    });

    fs::error_code ec;

    if (!fs::exists(name) && fs::create_directory(name, ec)) {
      return name;
    } else if (ec) {
      *err = "could not create directory '" + name + "': " + ec.message();
    }
  }

  *err = "failed create random directory after " + std::to_string(tries) +
         " tries";
  return {};
}
}  // anonymous namespace

StateTestWithBuiltinRules::StateTestWithBuiltinRules() {
  AddCatRule(&state_);
}

void StateTestWithBuiltinRules::AddCatRule(State* state) {
  AssertParse(state,
              "rule cat\n"
              "  command = cat $in > $out\n");
}

Node* StateTestWithBuiltinRules::GetNode(const std::string& path) {
  EXPECT_FALSE(strpbrk(path.c_str(), "/\\"));
  return state_.GetNode(path, 0);
}

void AssertParse(State* state, const char* input, ManifestParserOptions opts) {
  ManifestParser parser(state, nullptr, opts);
  std::string err;
  EXPECT_TRUE(parser.ParseTest(input, &err));
  ASSERT_EQ("", err);
  VerifyGraph(*state);
}

void AssertHash(const char* expected, uint64_t actual) {
  ASSERT_EQ(BuildLog::HashCommand(expected), actual);
}

void VerifyGraph(const State& state) {
  for (const auto& e : state.edges_) {
    // All edges need at least one output.
    EXPECT_FALSE(e->outputs_.empty());
    // Check that the edge's inputs have the edge as out-edge.
    for (const auto& in_node : e->inputs_) {
      const std::vector<Edge*>& out_edges = in_node->out_edges();
      EXPECT_NE(find(out_edges.begin(), out_edges.end(), e.get()),
                out_edges.end());
    }
    // Check that the edge's outputs have the edge as in-edge.
    for (const auto& out_node : e->outputs_) {
      EXPECT_EQ(out_node->in_edge(), e.get());
    }
  }

  // The union of all in- and out-edges of each nodes should be exactly edges_.
  std::set<const Edge*> node_edge_set;
  for (const auto& [path, n] : state.paths_) {
    (void)path;
    if (n->in_edge())
      node_edge_set.insert(n->in_edge());
    node_edge_set.insert(n->out_edges().begin(), n->out_edges().end());
  }
  std::set<const Edge*> edge_set;
  std::transform(state.edges_.begin(), state.edges_.end(),
                 std::inserter(edge_set, edge_set.end()),
                 [](const auto& e) { return e.get(); });
  EXPECT_EQ(node_edge_set, edge_set);
}

void VirtualFileSystem::Create(const std::string& path,
                               const std::string& contents) {
  files_[path].mtime = now_;
  files_[path].contents = contents;
  files_created_.insert(path);
}

TimeStamp VirtualFileSystem::Stat(const std::string& path,
                                  std::string* err) const {
  FileMap::const_iterator i = files_.find(path);
  if (i != files_.end()) {
    *err = i->second.stat_error;
    return i->second.mtime;
  }
  return 0;
}

bool VirtualFileSystem::WriteFile(const std::string& path,
                                  const std::string& contents) {
  Create(path, contents);
  return true;
}

bool VirtualFileSystem::MakeDir(const std::string& path) {
  directories_made_.push_back(path);
  return true;  // success
}

FileReader::Status VirtualFileSystem::ReadFile(const std::string& path,
                                               std::string* contents,
                                               std::string* err) {
  files_read_.push_back(path);
  FileMap::iterator i = files_.find(path);
  if (i != files_.end()) {
    *contents = i->second.contents;
    return Okay;
  }
  *err = strerror(ENOENT);
  return NotFound;
}

int VirtualFileSystem::RemoveFile(const std::string& path) {
  if (find(directories_made_.begin(), directories_made_.end(), path) !=
      directories_made_.end())
    return -1;
  FileMap::iterator i = files_.find(path);
  if (i != files_.end()) {
    files_.erase(i);
    files_removed_.insert(path);
    return 0;
  } else {
    return 1;
  }
}

void ScopedTempDir::CreateAndEnter(const std::string& name) {
  fs::error_code ec;

  // First change into the system temp dir and save it for cleanup.
  start_dir_ = fs::temp_directory_path(ec);
  if (ec)
    Fatal("couldn't get system temp dir: %s", ec.message().c_str());
  fs::current_path(start_dir_, ec);
  if (ec)
    Fatal("chdir: %s", ec.message().c_str());

  // Create a temporary subdirectory of that.
  std::string err;
  temp_dir_name_ = MakeTempDir(name, &err);

  if (!err.empty()) {
    Fatal("MakeTempDir: %s", err.c_str());
  }

  // chdir into the new temporary directory.
  fs::current_path(temp_dir_name_, ec);
  if (ec)
    Fatal("chdir: %s", ec.message().c_str());
}

void ScopedTempDir::Cleanup() {
  if (temp_dir_name_.empty())
    return;  // Something went wrong earlier.

  // Move out of the directory we're about to clobber.
  fs::error_code ec;
  fs::current_path(start_dir_, ec);
  if (ec)
    Fatal("chdir: %s", ec.message().c_str());

  fs::remove_all(temp_dir_name_);

  temp_dir_name_.clear();
}
