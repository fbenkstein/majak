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

#ifndef NINJA_TEST_H_
#define NINJA_TEST_H_

#include "disk_interface.h"
#include "filesystem.h"
#include "manifest_parser.h"
#include "state.h"
#include "util.h"

#include <gtest/gtest.h>

// Support utilites for tests.

namespace ninja {
struct Node;
}

/// A base test fixture that includes a State object with a
/// builtin "cat" rule.
struct StateTestWithBuiltinRules : public testing::Test {
  StateTestWithBuiltinRules();

  /// Add a "cat" rule to \a state.  Used by some tests; it's
  /// otherwise done by the ctor to state_.
  void AddCatRule(ninja::State* state);

  /// Short way to get a Node by its path from state_.
  ninja::Node* GetNode(const std::string& path);

  ninja::State state_;
};

void AssertParse(ninja::State* state, const char* input,
                 ninja::ManifestParserOptions = {});
void AssertHash(const char* expected, uint64_t actual);
void VerifyGraph(const ninja::State& state);

/// An implementation of DiskInterface that uses an in-memory representation
/// of disk state.  It also logs file accesses and directory creations
/// so it can be used by tests to verify disk access patterns.
struct VirtualFileSystem : public ninja::DiskInterface {
  VirtualFileSystem() : now_(1) {}

  /// "Create" a file with contents.
  void Create(const std::string& path, const std::string& contents);

  /// Tick "time" forwards; subsequent file operations will be newer than
  /// previous ones.
  int Tick() { return ++now_; }

  // DiskInterface
  virtual ninja::TimeStamp Stat(const std::string& path,
                                std::string* err) const;
  virtual bool WriteFile(const std::string& path, const std::string& contents);
  virtual bool MakeDir(const std::string& path);
  virtual Status ReadFile(const std::string& path, std::string* contents,
                          std::string* err);
  virtual int RemoveFile(const std::string& path);

  /// An entry for a single in-memory file.
  struct Entry {
    int mtime;
    std::string stat_error;  // If mtime is -1.
    std::string contents;
  };

  std::vector<std::string> directories_made_;
  std::vector<std::string> files_read_;
  typedef std::map<std::string, Entry> FileMap;
  FileMap files_;
  std::set<std::string> files_removed_;
  std::set<std::string> files_created_;

  /// A simple fake timestamp for file operations.
  int now_;
};

struct ScopedTempDir {
  /// Create a temporary directory and chdir into it.
  void CreateAndEnter(const std::string& name);

  /// Clean up the temporary directory.
  void Cleanup();

  /// The temp directory containing our dir.
  ninja::fs::path start_dir_;
  /// The subdirectory name for our dir, or empty if it hasn't been set up.
  std::string temp_dir_name_;
};

#endif  // NINJA_TEST_H_
