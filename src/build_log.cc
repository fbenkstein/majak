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
#include "build_log_generated.h"

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
#include "util.h"

#include "build_log_schema.h"

// Implementation details:
// Each run's log appends to the log file.
// To load, we run through all log entries in series, throwing away
// older runs.
// Once the number of redundant entries exceeds a threshold, we write
// out a new file and replace the existing one with it.

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

}  // namespace

// static
const char* const BuildLog::kFileSignature = "# majak log v1.%03d\n";
const int BuildLog::kCurrentVersion = 1;
const int BuildLog::kOldestSupportedVersion = 1;
const char* const BuildLog::kFilename = ".ninja_log";
const char* const BuildLog::kSchema = kBuildLogSchema;

uint64_t BuildLog::LogEntry::HashCommand(std::string_view command) {
  return MurmurHash64A(command.data(), command.size());
}

BuildLog::LogEntry::LogEntry(const std::string& output) : output(output) {}

BuildLog::LogEntry::LogEntry(const std::string& output, uint64_t command_hash,
                             int start_time, int end_time,
                             TimeStamp restat_mtime)
    : output(output), command_hash(command_hash), start_time(start_time),
      end_time(end_time), mtime(restat_mtime) {}

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
  setvbuf(log_file_, nullptr, _IOLBF, BUFSIZ);
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
  uint64_t command_hash = LogEntry::HashCommand(command);
  for (std::vector<Node*>::iterator out = edge->outputs_.begin();
       out != edge->outputs_.end(); ++out) {
    const std::string& path = (*out)->path();
    Entries::iterator i = entries_.find(path);
    LogEntry* log_entry;
    if (i != entries_.end()) {
      log_entry = i->second;
    } else {
      log_entry = new LogEntry(path);
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
  }
  return true;
}

void BuildLog::Close() {
  if (log_file_)
    fclose(log_file_);
  log_file_ = nullptr;
}

bool BuildLog::Load(const std::string& path, std::string* err) {
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
    ninja::error_code ec;
    ninja::fs::remove(path, ec);
    if (ec) {
      *err = "failed to remove invalid build log: " + ec.message();
      return false;
    }
    // Don't report this as a failure.  An empty build log will cause
    // us to rebuild the outputs anyway.
    return true;
  }

  int unique_entry_count = 0;
  int total_entry_count = 0;

  uint8_t size_buffer[sizeof(flatbuffers::uoffset_t)];
  std::vector<uint8_t> entry_buffer;

  for (;;) {
    if (fread(size_buffer, 1, sizeof(size_buffer), file) !=
        sizeof(size_buffer)) {
      break;
    }

    size_t entry_size = flatbuffers::GetPrefixedSize(size_buffer);

    entry_buffer.resize(std::max(entry_buffer.size(), entry_size));

    if (fread(entry_buffer.data(), 1, entry_size, file) != entry_size) {
      break;
    }

    auto* entry =
        flatbuffers::GetRoot<ninja::BuildLogEntry>(entry_buffer.data());

    if (!entry->output()) {
      break;
    }

    std::string_view output(entry->output()->c_str(),
                            entry->output()->Length());

    Entries::iterator i = entries_.find(output);
    LogEntry* log_entry;

    if (i != entries_.end()) {
      log_entry = i->second;
    } else {
      log_entry = new LogEntry(std::string(output));
      entries_.insert(Entries::value_type(log_entry->output, log_entry));
      ++unique_entry_count;
    }
    ++total_entry_count;

    log_entry->start_time = entry->start_time();
    log_entry->end_time = entry->end_time();
    log_entry->mtime = entry->mtime();
    log_entry->command_hash = entry->command_hash();
  }
  fclose(file);

  // Decide whether it's time to rebuild the log:
  // - if we're upgrading versions
  // - if it's getting large
  int kMinCompactionEntryCount = 100;
  int kCompactionRatio = 3;
  if (log_version < kCurrentVersion) {
    needs_recompaction_ = true;
  } else if (total_entry_count > kMinCompactionEntryCount &&
             total_entry_count > unique_entry_count * kCompactionRatio) {
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

bool BuildLog::WriteEntry(FILE* f, const LogEntry& entry) {
  fbb_.Clear();
  auto offset = ninja::CreateBuildLogEntryDirect(
      fbb_, entry.output.c_str(), entry.command_hash, entry.start_time,
      entry.end_time, entry.mtime);
  fbb_.FinishSizePrefixed(offset);
  return fwrite(fbb_.GetBufferPointer(), 1, fbb_.GetSize(), f) ==
         fbb_.GetSize();
}

bool BuildLog::Recompact(const std::string& path, const BuildLogUser& user,
                         std::string* err) {
  METRIC_RECORD(".ninja_log recompact");

  Close();
  std::string temp_path = path + ".recompact";
  FILE* f = fopen(temp_path.c_str(), "wb");
  if (!f) {
    *err = strerror(errno);
    return false;
  }

  if (fprintf(f, kFileSignature, kCurrentVersion) < 0) {
    *err = strerror(errno);
    fclose(f);
    return false;
  }

  std::vector<std::string_view> dead_outputs;
  for (Entries::iterator i = entries_.begin(); i != entries_.end(); ++i) {
    if (user.IsPathDead(i->first)) {
      dead_outputs.push_back(i->first);
      continue;
    }

    if (!WriteEntry(f, *i->second)) {
      *err = strerror(errno);
      fclose(f);
      return false;
    }
  }

  for (size_t i = 0; i < dead_outputs.size(); ++i)
    entries_.erase(dead_outputs[i]);

  fclose(f);
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
