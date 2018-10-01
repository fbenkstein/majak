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

#ifndef NINJA_BUILD_LOG_H_
#define NINJA_BUILD_LOG_H_

#include <stdio.h>
#include <string>

#include <flatbuffers/flatbuffers.h>

#include "hash_map.h"
#include "log_generated.h"
#include "timestamp.h"
#include "util.h"  // uint64_t

namespace ninja {

struct Edge;

struct Node;
struct State;

/// Can answer questions about the manifest for the BuildLog.
struct BuildLogUser {
  /// Return if a given output is no longer part of the build manifest.
  /// This is only called during recompaction and doesn't have to be fast.
  virtual bool IsPathDead(std::string_view s) const = 0;
};

/// Store a log of every command ran for every build.
/// It has a few uses:
///
/// 1) (hashes of) command lines for existing output files, so we know
///    when we need to rebuild due to the command changing
/// 2) timing information, perhaps for generating reports
/// 3) restat information
struct BuildLog {
  static const char* const kFileSignature;
  static const int kCurrentVersion;
  static const int kOldestSupportedVersion;
  static const char* const kFilename;
  static const char* const kSchema;

  BuildLog();
  ~BuildLog();

  // Writing (build-time) interface.
  bool OpenForWrite(const std::string& path, const BuildLogUser& user,
                    std::string* err);
  bool RecordCommand(Edge* edge, int start_time, int end_time,
                     TimeStamp mtime = 0);
  bool RecordDeps(Node* node, TimeStamp mtime, const std::vector<Node*>& nodes);
  bool RecordDeps(Node* node, TimeStamp mtime, int node_count, Node** nodes);
  void Close();

  // Reading (startup-time) interface.
  struct Deps {
    Deps(int64_t mtime, int node_count)
        : mtime(mtime), node_count(node_count), nodes(new Node*[node_count]) {}
    ~Deps() { delete[] nodes; }
    TimeStamp mtime;
    int node_count;
    Node** nodes;
  };
  bool Load(const std::string& path, State* state, std::string* err);
  Deps* GetDeps(Node* node);

  static uint64_t HashCommand(std::string_view command);

  using LogEntry = log::BuildEntryT;

  /// Lookup a previously-run command by its output path.
  LogEntry* LookupByOutput(const std::string& path);

  /// Returns if the deps entry for a node is still reachable from the manifest.
  ///
  /// The deps log can contain deps entries for files that were built in the
  /// past but are no longer part of the manifest.  This function returns if
  /// this is the case for a given node.  This function is slow, don't call
  /// it from code that runs on every build.
  bool IsDepsEntryLiveFor(Node* node);

  /// Serialize an entry into a log file.
  bool WriteEntry(FILE* f, const LogEntry& entry);

  /// Rewrite the known log entries, throwing away old data.
  bool Recompact(const std::string& path, const BuildLogUser& user,
                 std::string* err);

  typedef ExternalStringHashMap<std::unique_ptr<LogEntry>>::Type Entries;
  const Entries& entries() const { return entries_; }

  /// Used for tests and tools.
  const std::vector<Node*>& nodes() const { return nodes_; }
  const std::vector<std::unique_ptr<Deps>>& deps() const { return deps_; }

 private:
  // Updates the in-memory representation.  Takes ownership of |deps|.
  // Returns true if a prior deps record was deleted.
  bool UpdateDeps(int out_id, std::unique_ptr<Deps> deps);
  // Write a node name record, assigning it an id.
  bool RecordId(Node* node);
  // Write a command record.
  bool RecordCommand(const std::string& path, uint64_t command_hash,
                     int start_time, int end_time, TimeStamp mtime);

  /// Maps id -> Node.
  std::vector<Node*> nodes_;
  /// Maps id -> deps of that id.
  std::vector<std::unique_ptr<Deps>> deps_;
  /// Maps output name -> log entry.
  Entries entries_;
  FILE* log_file_;
  bool needs_recompaction_;
  flatbuffers::FlatBufferBuilder fbb_;
};

bool operator==(const log::BuildEntryT& e1, const log::BuildEntryT& e2);
bool operator!=(const log::BuildEntryT& e1, const log::BuildEntryT& e2);

}  // namespace ninja

#endif  // NINJA_BUILD_LOG_H_
