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

#include "build_log.h"

#include "filesystem.h"
#include "test.h"
#include "util.h"

#include <sys/stat.h>
#ifdef _WIN32
#include <fcntl.h>
#include <share.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = ninja::fs;

namespace {

const char kTestFilename[] = "BuildLogTest-tempfile";

struct BuildLogTest : public StateTestWithBuiltinRules, public BuildLogUser {
  void RemoveTestFile() {
    ninja::error_code ignore;
    fs::remove(kTestFilename, ignore);
  }
  virtual void SetUp() {
    // In case a crashing test left a stale file behind.
    RemoveTestFile();
  }
  virtual void TearDown() { RemoveTestFile(); }
  virtual bool IsPathDead(std::string_view s) const { return false; }
};

TEST_F(BuildLogTest, WriteRead) {
  AssertParse(&state_,
              "build out: cat mid\n"
              "build mid: cat in\n");

  BuildLog log1;
  std::string err;
  EXPECT_TRUE(log1.OpenForWrite(kTestFilename, *this, &err));
  ASSERT_EQ("", err);
  log1.RecordCommand(state_.edges_[0], 15, 18);
  log1.RecordCommand(state_.edges_[1], 20, 25);
  log1.Close();

  BuildLog log2;
  EXPECT_TRUE(log2.Load(kTestFilename, &err));
  ASSERT_EQ("", err);

  ASSERT_EQ(2u, log1.entries().size());
  ASSERT_EQ(2u, log2.entries().size());
  BuildLog::LogEntry* e1 = log1.LookupByOutput("out");
  ASSERT_TRUE(e1);
  BuildLog::LogEntry* e2 = log2.LookupByOutput("out");
  ASSERT_TRUE(e2);
  ASSERT_TRUE(*e1 == *e2);
  ASSERT_EQ(15, e1->start_time);
  ASSERT_EQ("out", e1->output);
}

TEST_F(BuildLogTest, FirstWriteAddsSignature) {
  const std::string kExpectedSignature = "# majak log v";

  BuildLog log;
  std::string contents, err;

  EXPECT_TRUE(log.OpenForWrite(kTestFilename, *this, &err));
  ASSERT_EQ("", err);
  log.Close();

  ASSERT_EQ(0, ReadFile(kTestFilename, &contents, &err));
  ASSERT_EQ("", err);
  ASSERT_LT(kExpectedSignature.size(), contents.size());
  EXPECT_EQ(kExpectedSignature, contents.substr(0, kExpectedSignature.size()));
  EXPECT_EQ('\n', contents.back());

  // Opening the file anew shouldn't add a second version string.
  EXPECT_TRUE(log.OpenForWrite(kTestFilename, *this, &err));
  ASSERT_EQ("", err);
  log.Close();

  contents.clear();
  ASSERT_EQ(0, ReadFile(kTestFilename, &contents, &err));
  ASSERT_EQ("", err);
  ASSERT_LT(kExpectedSignature.size(), contents.size());
  EXPECT_EQ(kExpectedSignature, contents.substr(0, kExpectedSignature.size()));
  EXPECT_EQ('\n', contents.back());
}

namespace {
BuildLog::LogEntry LogEntry(std::string output, uint64_t command_hash,
                            uint32_t start_time, uint32_t end_time,
                            uint64_t mtime) {
  BuildLog::LogEntry e;
  e.output = std::move(output);
  e.command_hash = command_hash;
  e.start_time = start_time;
  e.end_time = end_time;
  e.mtime = mtime;
  return e;
}
}  // anonymous namespace

TEST_F(BuildLogTest, DoubleEntry) {
  std::string err;
  {
    BuildLog log;
    EXPECT_TRUE(log.OpenForWrite(kTestFilename, *this, &err));
    log.Close();
    FILE* f = fopen(kTestFilename, "ab");
    log.WriteEntry(
        f, LogEntry("out", BuildLog::HashCommand("command abc"), 0, 1, 2));
    log.WriteEntry(
        f, LogEntry("out", BuildLog::HashCommand("command def"), 3, 4, 5));
    fclose(f);
  }

  BuildLog log;
  EXPECT_TRUE(log.Load(kTestFilename, &err));
  ASSERT_EQ("", err);

  BuildLog::LogEntry* e = log.LookupByOutput("out");
  ASSERT_TRUE(e);
  ASSERT_NO_FATAL_FAILURE(AssertHash("command def", e->command_hash));
}

TEST_F(BuildLogTest, Truncate) {
  AssertParse(&state_,
              "build out: cat mid\n"
              "build mid: cat in\n");

  {
    BuildLog log1;
    std::string err;
    EXPECT_TRUE(log1.OpenForWrite(kTestFilename, *this, &err));
    ASSERT_EQ("", err);
    log1.RecordCommand(state_.edges_[0], 15, 18);
    log1.RecordCommand(state_.edges_[1], 20, 25);
    log1.Close();
  }

  struct stat statbuf;
  ASSERT_EQ(0, stat(kTestFilename, &statbuf));
  ASSERT_GT(statbuf.st_size, 0);

  // For all possible truncations of the input file, assert that we don't
  // crash when parsing.
  for (off_t size = statbuf.st_size; size > 0; --size) {
    BuildLog log2;
    std::string err;
    EXPECT_TRUE(log2.OpenForWrite(kTestFilename, *this, &err));
    ASSERT_EQ("", err);
    log2.RecordCommand(state_.edges_[0], 15, 18);
    log2.RecordCommand(state_.edges_[1], 20, 25);
    log2.Close();

    ASSERT_TRUE(Truncate(kTestFilename, size, &err));

    BuildLog log3;
    err.clear();
    ASSERT_TRUE(log3.Load(kTestFilename, &err) || !err.empty());
  }
}

TEST_F(BuildLogTest, ObsoleteOldVersion) {
  FILE* f = fopen(kTestFilename, "wb");
  fprintf(f, "# ninja log v5\n");
  fprintf(f, "123\t456\t0\tout\t789\n");
  fclose(f);

  std::string err;
  BuildLog log;
  EXPECT_TRUE(log.Load(kTestFilename, &err));
  ASSERT_NE(err.find("version"), std::string::npos);
}

TEST_F(BuildLogTest, DuplicateVersionHeader) {
  // Old versions of ninja accidentally wrote multiple version headers to the
  // build log on Windows. This shouldn't crash, and the second version header
  // should be ignored.
  FILE* f = fopen(kTestFilename, "wb");
  fprintf(f, "# ninja log v4\n");
  fprintf(f, "123\t456\t456\tout\tcommand\n");
  fprintf(f, "# ninja log v4\n");
  fprintf(f, "456\t789\t789\tout2\tcommand2\n");
  fclose(f);

  std::string err;
  BuildLog log;
  EXPECT_TRUE(log.Load(kTestFilename, &err));
  EXPECT_NE(err.find("version"), std::string::npos);
  EXPECT_FALSE(fs::exists(kTestFilename));
}

TEST_F(BuildLogTest, MultiTargetEdge) {
  AssertParse(&state_, "build out out.d: cat\n");

  BuildLog log;
  log.RecordCommand(state_.edges_[0], 21, 22);

  ASSERT_EQ(2u, log.entries().size());
  BuildLog::LogEntry* e1 = log.LookupByOutput("out");
  ASSERT_TRUE(e1);
  BuildLog::LogEntry* e2 = log.LookupByOutput("out.d");
  ASSERT_TRUE(e2);
  ASSERT_EQ("out", e1->output);
  ASSERT_EQ("out.d", e2->output);
  ASSERT_EQ(21, e1->start_time);
  ASSERT_EQ(21, e2->start_time);
  ASSERT_EQ(22, e2->end_time);
  ASSERT_EQ(22, e2->end_time);
}

struct BuildLogRecompactTest : public BuildLogTest {
  virtual bool IsPathDead(std::string_view s) const { return s == "out2"; }
};

TEST_F(BuildLogRecompactTest, Recompact) {
  AssertParse(&state_,
              "build out: cat in\n"
              "build out2: cat in\n");

  BuildLog log1;
  std::string err;
  EXPECT_TRUE(log1.OpenForWrite(kTestFilename, *this, &err));
  ASSERT_EQ("", err);
  // Record the same edge several times, to trigger recompaction
  // the next time the log is opened.
  for (int i = 0; i < 200; ++i)
    log1.RecordCommand(state_.edges_[0], 15, 18 + i);
  log1.RecordCommand(state_.edges_[1], 21, 22);
  log1.Close();

  // Load...
  BuildLog log2;
  EXPECT_TRUE(log2.Load(kTestFilename, &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(2u, log2.entries().size());
  ASSERT_TRUE(log2.LookupByOutput("out"));
  ASSERT_TRUE(log2.LookupByOutput("out2"));
  // ...and force a recompaction.
  EXPECT_TRUE(log2.OpenForWrite(kTestFilename, *this, &err));
  log2.Close();

  // "out2" is dead, it should've been removed.
  BuildLog log3;
  EXPECT_TRUE(log2.Load(kTestFilename, &err));
  ASSERT_EQ("", err);
  ASSERT_EQ(1u, log2.entries().size());
  ASSERT_TRUE(log2.LookupByOutput("out"));
  ASSERT_FALSE(log2.LookupByOutput("out2"));
}

}  // anonymous namespace
