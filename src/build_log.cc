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

// On AIX, inttypes.h gets indirectly included by build_log.h.
// It's easiest just to ask for the printf format macros right away.
#ifndef _WIN32
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#endif

#include "build_log.h"
#include "log_generated.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <inttypes.h>
#include <unistd.h>
#endif

#include "build.h"
#include "filesystem.h"
#include "graph.h"
#include "metrics.h"
#include "state.h"
#include "util.h"

#include "log_schema.h"

namespace ninja {

// Implementation details:
// Each run's log appends to the log file.
// To load, we run through all log entries in series, throwing away
// older runs.
// Once the number of redundant entries exceeds a threshold, we write
// out a new file and replace the existing one with it.

// As build commands run they can output extra dependency information
// (e.g. header dependencies for C source) dynamically.  DepsLog collects
// that information at build time and uses it for subsequent builds.
//
// The on-disk format is based on two primary design constraints:
// - it must be written to as a stream (during the build, which may be
//   interrupted);
// - it can be read all at once on startup.  (Alternative designs, where
//   it contains indexing information, were considered and discarded as
//   too complicated to implement; if the file is small than reading it
//   fully on startup is acceptable.)
// Here are some stats from the Windows Chrome dependency files, to
// help guide the design space.  The total text in the files sums to
// 90mb so some compression is warranted to keep load-time fast.
// There's about 10k files worth of dependencies that reference about
// 40k total paths totalling 2mb of unique strings.
//
// Based on these stats, here's the current design.
// The file is structured as version header followed by a sequence of records.
// Each record is either a path string or a dependency list.
// Numbering the path strings in file order gives them dense integer ids.
// A dependency list maps an output id to a list of input ids.
//
// Concretely, a record is:
//    four bytes record length, high bit indicates record type
//      (but max record sizes are capped at 512kB)
//    path records contain the string name of the path, followed by up to 3
//      padding bytes to align on 4 byte boundaries, followed by the
//      one's complement of the expected index of the record (to detect
//      concurrent writes of multiple ninja processes to the log).
//    dependency records are an array of 4-byte integers
//      [output path id,
//       output path mtime (lower 4 bytes), output path mtime (upper 4 bytes),
//       input path id, input path id...]
//      (The mtime is compared against the on-disk output path mtime
//      to verify the stored data is up-to-date.)
// If two records reference the same output the latter one in the file
// wins, allowing updates to just be appended to the file.  A separate
// repacking step can run occasionally to remove dead records.
//
// The above is almost accurate, except that we write log entries though
// flatbuffers and not manually anymore.

namespace {

// 64bit MurmurHash2, by Austin Appleby
#if defined(_MSC_VER)
#define BIG_CONSTANT(x) (x)
#else  // defined(_MSC_VER)
#define BIG_CONSTANT(x) (x##LLU)
#endif  // !defined(_MSC_VER)
inline uint64_t MurmurHash64A(const void* key, size_t len) {
  static const uint64_t seed = 0xDECAFBADDECAFBADull;
  const uint64_t m = BIG_CONSTANT(0xc6a4a7935bd1e995);
  const int r = 47;
  uint64_t h = seed ^ (len * m);
  const unsigned char* data = (const unsigned char*)key;
  while (len >= 8) {
    uint64_t k;
    memcpy(&k, data, sizeof k);
    k *= m;
    k ^= k >> r;
    k *= m;
    h ^= k;
    h *= m;
    data += 8;
    len -= 8;
  }
  switch (len & 7) {
  case 7:
    h ^= uint64_t(data[6]) << 48;
  case 6:
    h ^= uint64_t(data[5]) << 40;
  case 5:
    h ^= uint64_t(data[4]) << 32;
  case 4:
    h ^= uint64_t(data[3]) << 24;
  case 3:
    h ^= uint64_t(data[2]) << 16;
  case 2:
    h ^= uint64_t(data[1]) << 8;
  case 1:
    h ^= uint64_t(data[0]);
    h *= m;
  };
  h ^= h >> r;
  h *= m;
  h ^= h >> r;
  return h;
}
#undef BIG_CONSTANT

// Record size is currently limited to less than the full 32 bit, to
// be able to control flushing manually.
const unsigned kMaxRecordSize = (1 << 20) - 1;
}  // namespace

// static
const char* const BuildLog::kFileSignature = "# majak log v1.%03d\n";
const int BuildLog::kCurrentVersion = 2;
const int BuildLog::kOldestSupportedVersion = 2;
const char* const BuildLog::kFilename = ".ninja_log";
const char* const BuildLog::kSchema = kBuildLogSchema;

uint64_t BuildLog::HashCommand(std::string_view command) {
  return MurmurHash64A(command.data(), command.size());
}

BuildLog::BuildLog() : log_file_(nullptr), needs_recompaction_(false) {}

BuildLog::~BuildLog() {
  Close();
}

bool BuildLog::OpenForWrite(const std::string& path, const BuildLogUser& user,
                            std::string* err) {
  if (needs_recompaction_) {
    if (!Recompact(path, user, err))
      return false;
  }

  log_file_ = fopen(path.c_str(), "ab");
  if (!log_file_) {
    *err = strerror(errno);
    return false;
  }
  // Set the buffer size to this and flush the file buffer after every record
  // to make sure records aren't written partially.
  setvbuf(log_file_, nullptr, _IOLBF, kMaxRecordSize);
  SetCloseOnExec(fileno(log_file_));

  // Opening a file in append mode doesn't set the file pointer to the file's
  // end on Windows. Do that explicitly.
  fseek(log_file_, 0, SEEK_END);

  if (ftell(log_file_) == 0) {
    if (fprintf(log_file_, kFileSignature, kCurrentVersion) < 0) {
      *err = strerror(errno);
      return false;
    }
  }

  return true;
}

bool BuildLog::RecordCommand(Edge* edge, int start_time, int end_time,
                             TimeStamp mtime) {
  std::string command = edge->EvaluateCommand(true);
  uint64_t command_hash = HashCommand(command);
  for (std::vector<Node*>::iterator out = edge->outputs_.begin();
       out != edge->outputs_.end(); ++out) {
    if (!RecordCommand((*out)->path(), command_hash, start_time, end_time,
                       mtime))
      return false;
  }

  return true;
}

bool BuildLog::RecordCommand(const std::string& path, uint64_t command_hash,
                             int start_time, int end_time, TimeStamp mtime) {
  Entries::iterator i = entries_.find(path);
  LogEntry* log_entry;
  if (i != entries_.end()) {
    log_entry = i->second;
  } else {
    log_entry = new LogEntry;
    log_entry->output = path;
    entries_.insert(Entries::value_type(log_entry->output, log_entry));
  }
  log_entry->command_hash = command_hash;
  log_entry->start_time = start_time;
  log_entry->end_time = end_time;
  log_entry->mtime = mtime;

  if (log_file_) {
    if (!WriteEntry(log_file_, *log_entry))
      return false;
    if (fflush(log_file_) != 0) {
      return false;
    }
  }
  return true;
}

bool BuildLog::RecordDeps(Node* node, TimeStamp mtime,
                          const std::vector<Node*>& nodes) {
  return RecordDeps(node, mtime, nodes.size(),
                    nodes.empty() ? nullptr : (Node**)&nodes.front());
}

bool BuildLog::RecordDeps(Node* node, TimeStamp mtime, int node_count,
                          Node** nodes) {
  // Track whether there's any new data to be recorded.
  bool made_change = false;

  // Assign ids to all nodes that are missing one.
  if (node->id() < 0) {
    if (!RecordId(node))
      return false;
    made_change = true;
  }
  for (int i = 0; i < node_count; ++i) {
    if (nodes[i]->id() < 0) {
      if (!RecordId(nodes[i]))
        return false;
      made_change = true;
    }
  }

  // See if the new data is different than the existing data, if any.
  if (!made_change) {
    Deps* deps = GetDeps(node);
    if (!deps || deps->mtime != mtime || deps->node_count != node_count) {
      made_change = true;
    } else {
      for (int i = 0; i < node_count; ++i) {
        if (deps->nodes[i] != nodes[i]) {
          made_change = true;
          break;
        }
      }
    }
  }

  // Don't write anything if there's no new info.
  if (!made_change)
    return true;

  log::EntryHolderT entry_holder;

  {
    log::DepsEntryT entry;
    entry.output = node->id();
    entry.mtime = mtime;
    entry.deps.reserve(node_count);

    for (int i = 0; i < node_count; ++i) {
      entry.deps.push_back(nodes[i]->id());
    }

    entry_holder.entry.Set(std::move(entry));
  }

  fbb_.Clear();
  auto offset = log::CreateEntryHolder(fbb_, &entry_holder);
  fbb_.FinishSizePrefixed(offset);
  assert(fbb_.GetSize() < kMaxRecordSize);
  if (fwrite(fbb_.GetBufferPointer(), 1, fbb_.GetSize(), log_file_) !=
      fbb_.GetSize())
    return false;

  if (fflush(log_file_) != 0)
    return false;

  // Update in-memory representation.
  Deps* deps = new Deps(mtime, node_count);
  for (int i = 0; i < node_count; ++i)
    deps->nodes[i] = nodes[i];
  UpdateDeps(node->id(), deps);

  return true;
}

void BuildLog::Close() {
  if (log_file_)
    fclose(log_file_);
  log_file_ = nullptr;
}

bool BuildLog::Load(const std::string& path, State* state, std::string* err) {
  METRIC_RECORD(".ninja_log load");
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) {
    if (errno == ENOENT)
      return true;
    *err = strerror(errno);
    return false;
  }

  int log_version = 0;
  size_t signature_length =
      std::snprintf(nullptr, 0, kFileSignature, kCurrentVersion);
  std::string signature(signature_length, '\x00');

  if (fread(signature.data(), 1, signature.size(), file) != signature_length ||
      sscanf(signature.data(), kFileSignature, &log_version) != 1 ||
      log_version < kOldestSupportedVersion || log_version > kCurrentVersion) {
    *err =
        ("build log version invalid, perhaps due to being too old; "
         "starting over");
    fclose(file);
    fs::error_code ec;
    fs::remove(path, ec);
    if (ec) {
      *err = "failed to remove invalid build log: " + ec.message();
      return false;
    }
    // Don't report this as a failure.  An empty build log will cause
    // us to rebuild the outputs anyway.
    return true;
  }

  long offset;
  bool read_failed = false;
  int unique_entry_count = 0;
  int total_entry_count = 0;
  int unique_dep_record_count = 0;
  int total_dep_record_count = 0;

  uint8_t size_buffer[sizeof(flatbuffers::uoffset_t)];
  std::vector<uint8_t> entry_buffer;

  for (;;) {
    offset = ftell(file);

    if (size_t bytes_read = fread(size_buffer, 1, sizeof(size_buffer), file);
        bytes_read != sizeof(size_buffer)) {
      read_failed = bytes_read != 0;
      break;
    }

    size_t entry_size = flatbuffers::GetPrefixedSize(size_buffer);

    entry_buffer.resize(std::max(entry_buffer.size(), entry_size));

    if (fread(entry_buffer.data(), 1, entry_size, file) != entry_size) {
      read_failed = true;
      break;
    }

    auto* entry_holder =
        flatbuffers::GetRoot<log::EntryHolder>(entry_buffer.data());

    flatbuffers::Verifier verifier(entry_buffer.data(), entry_buffer.size());

    if (!entry_holder->Verify(verifier)) {
      read_failed = true;
      break;
    }

    if (auto build_entry = entry_holder->entry_as_BuildEntry()) {
      std::string_view output(build_entry->output()->c_str(),
                              build_entry->output()->Length());

      Entries::iterator i = entries_.find(output);
      LogEntry* log_entry;

      if (i != entries_.end()) {
        log_entry = i->second;
      } else {
        log_entry = new LogEntry;
        log_entry->output = output;
        entries_.insert(Entries::value_type(log_entry->output, log_entry));
        ++unique_entry_count;
      }
      ++total_entry_count;

      log_entry->start_time = build_entry->start_time();
      log_entry->end_time = build_entry->end_time();
      log_entry->mtime = build_entry->mtime();
      log_entry->command_hash = build_entry->command_hash();
    } else if (auto path_entry = entry_holder->entry_as_PathEntry()) {
      const flatbuffers::String* deps_path = path_entry->path();

      // It is not necessary to pass in a correct slash_bits here. It will
      // either be a Node that's in the manifest (in which case it will
      // already have a correct slash_bits that GetNode will look up), or it
      // is an implicit dependency from a .d which does not affect the build
      // command (and so need not have its slashes maintained).
      Node* node = state->GetNode(
          std::string_view(deps_path->c_str(), deps_path->size()), 0);
      int expected_id = ~path_entry->checksum();
      int id = nodes_.size();
      if (id != expected_id) {
        read_failed = true;
        break;
      }

      assert(node->id() < 0);
      node->set_id(id);
      nodes_.push_back(node);
    } else if (auto deps_entry = entry_holder->entry_as_DepsEntry()) {
      const auto& deps_data = *deps_entry->deps();
      int deps_count = deps_data.size();
      int out_id = deps_entry->output();
      Deps* deps = new Deps(deps_entry->mtime(), deps_count);

      for (int i = 0; i < deps_count; ++i) {
        assert(deps_data[i] < nodes_.size());
        assert(nodes_[deps_data[i]]);
        deps->nodes[i] = nodes_[deps_data[i]];
      }

      total_dep_record_count++;
      if (!UpdateDeps(out_id, deps))
        ++unique_dep_record_count;
    }
  }

  if (read_failed) {
    // An error occurred while loading; try to recover by truncating the
    // file to the last fully-read record.
    if (ferror(file)) {
      *err = strerror(ferror(file));
    } else {
      *err = "premature end of file";
    }
    fclose(file);

    if (!Truncate(path, offset, err))
      return false;

    // The truncate succeeded; we'll just report the load error as a
    // warning because the build can proceed.
    *err += "; recovering";
    return true;
  }

  fclose(file);

  // Decide whether it's time to rebuild the log:
  // - if we're upgrading versions
  // - if it's getting large
  int kMinCompactionEntryCount = 100;
  int kMinCompactionDepsEntryCount = 1000;
  int kCompactionRatio = 3;
  if (log_version < kCurrentVersion) {
    needs_recompaction_ = true;
  } else if (total_entry_count > kMinCompactionEntryCount &&
             total_entry_count > unique_entry_count * kCompactionRatio) {
    needs_recompaction_ = true;
  } else if (total_dep_record_count > kMinCompactionDepsEntryCount &&
             total_dep_record_count >
                 unique_dep_record_count * kCompactionRatio) {
    needs_recompaction_ = true;
  }

  return true;
}

BuildLog::LogEntry* BuildLog::LookupByOutput(const std::string& path) {
  Entries::iterator i = entries_.find(path);
  if (i != entries_.end())
    return i->second;
  return nullptr;
}

BuildLog::Deps* BuildLog::GetDeps(Node* node) {
  // Abort if the node has no id (never referenced in the deps) or if
  // there's no deps recorded for the node.
  if (node->id() < 0 || node->id() >= (int)deps_.size())
    return nullptr;
  return deps_[node->id()];
}

bool BuildLog::WriteEntry(FILE* f, const LogEntry& entry) {
  log::EntryHolderT entry_holder;
  entry_holder.entry.Set(LogEntry(entry));
  fbb_.Clear();
  auto offset = log::CreateEntryHolder(fbb_, &entry_holder);
  fbb_.FinishSizePrefixed(offset);
  assert(fbb_.GetSize() < kMaxRecordSize);
  return fwrite(fbb_.GetBufferPointer(), 1, fbb_.GetSize(), f) ==
             fbb_.GetSize() &&
         fflush(f) == 0;
}

bool BuildLog::Recompact(const std::string& path, const BuildLogUser& user,
                         std::string* err) {
  METRIC_RECORD(".ninja_log recompact");

  Close();
  std::string temp_path = path + ".recompact";

  // OpenForWrite() opens for append.  Make sure it's not appending to a
  // left-over file from a previous recompaction attempt that crashed somehow.
  unlink(temp_path.c_str());

  BuildLog new_log;

  if (!new_log.OpenForWrite(temp_path, user, err))
    return false;

  // Write out all entries but skip dead paths.
  for (const auto& [output, entry] : entries_) {
    if (user.IsPathDead(output))
      continue;

    if (!new_log.RecordCommand(entry->output, entry->command_hash,
                               entry->start_time, entry->end_time,
                               entry->mtime)) {
      *err = strerror(errno);
      unlink(temp_path.c_str());
      return false;
    }
  }

  // Clear all known ids so that new ones can be reassigned.  The new indices
  // will refer to the ordering in new_log, not in the current log.
  for (auto& node : nodes_)
    node->set_id(-1);

  // Write out all deps again.
  for (int old_id = 0; old_id < (int)deps_.size(); ++old_id) {
    Deps* deps = deps_[old_id];
    if (!deps)
      continue;  // If nodes_[old_id] is a leaf, it has no deps.

    if (!IsDepsEntryLiveFor(nodes_[old_id]))
      continue;

    if (!new_log.RecordDeps(nodes_[old_id], deps->mtime, deps->node_count,
                            deps->nodes)) {
      *err = strerror(errno);
      unlink(temp_path.c_str());
      return false;
    }
  }

  new_log.Close();

  // Steal the new log's data.
  nodes_ = std::move(new_log.nodes_);
  deps_ = std::move(new_log.deps_);
  entries_ = std::move(new_log.entries_);

  if (unlink(path.c_str()) < 0) {
    *err = strerror(errno);
    return false;
  }

  if (rename(temp_path.c_str(), path.c_str()) < 0) {
    *err = strerror(errno);
    return false;
  }

  return true;
}

bool BuildLog::UpdateDeps(int out_id, Deps* deps) {
  if (out_id >= (int)deps_.size())
    deps_.resize(out_id + 1);

  bool delete_old = deps_[out_id] != nullptr;
  if (delete_old)
    delete deps_[out_id];
  deps_[out_id] = deps;
  return delete_old;
}

bool BuildLog::RecordId(Node* node) {
  int id = nodes_.size();

  log::EntryHolderT entry_holder;

  {
    log::PathEntryT entry;
    entry.path = node->path();
    entry.checksum = ~static_cast<uint32_t>(id);
    entry_holder.entry.Set(std::move(entry));
  }

  fbb_.Clear();
  auto offset = log::CreateEntryHolder(fbb_, &entry_holder);
  fbb_.FinishSizePrefixed(offset);
  assert(fbb_.GetSize() < kMaxRecordSize);
  if (fwrite(fbb_.GetBufferPointer(), 1, fbb_.GetSize(), log_file_) !=
      fbb_.GetSize())
    return false;

  if (fflush(log_file_) != 0)
    return false;

  node->set_id(id);
  nodes_.push_back(node);

  return true;
}

bool BuildLog::IsDepsEntryLiveFor(Node* node) {
  // Skip entries that don't have in-edges or whose edges don't have a
  // "deps" attribute. They were in the deps log from previous builds, but
  // the the files they were for were removed from the build and their deps
  // entries are no longer needed.
  // (Without the check for "deps", a chain of two or more nodes that each
  // had deps wouldn't be collected in a single recompaction.)
  return node->in_edge() && !node->in_edge()->GetBinding("deps").empty();
}

bool operator==(const log::BuildEntryT& e1, const log::BuildEntryT& e2) {
  auto to_tuple = [](const auto& e) {
    return std::tie(e.output, e.command_hash, e.start_time, e.end_time,
                    e.mtime);
  };
  return to_tuple(e1) == to_tuple(e2);
}

bool operator!=(const log::BuildEntryT& e1, const log::BuildEntryT& e2) {
  return !(e1 == e2);
}
}  // namespace ninja
