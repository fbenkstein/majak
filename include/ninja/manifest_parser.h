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

#ifndef NINJA_MANIFEST_PARSER_H_
#define NINJA_MANIFEST_PARSER_H_

#include <memory>
#include <string>

#include "lexer.h"

namespace ninja {

struct BindingEnv;
struct EvalString;
struct FileReader;
struct State;

enum DupeEdgeAction {
  kDupeEdgeActionWarn,
  kDupeEdgeActionError,
};

enum PhonyCycleAction {
  kPhonyCycleActionWarn,
  kPhonyCycleActionError,
};

struct ManifestParserOptions {
  ManifestParserOptions()
      : dupe_edge_action_(kDupeEdgeActionWarn),
        phony_cycle_action_(kPhonyCycleActionWarn) {}
  DupeEdgeAction dupe_edge_action_;
  PhonyCycleAction phony_cycle_action_;
};

/// Parses .ninja files.
struct ManifestParser {
  ManifestParser(State* state, FileReader* file_reader,
                 ManifestParserOptions options = ManifestParserOptions());

  /// Load and parse a file.
  bool Load(const std::string& filename, std::string* err,
            Lexer* parent = nullptr);

  /// Parse a text string of input.  Used by tests.
  bool ParseTest(const std::string& input, std::string* err) {
    quiet_ = true;
    return Parse("input", input, err);
  }

 private:
  /// Parse a file, given its contents as a string.
  bool Parse(const std::string& filename, const std::string& input,
             std::string* err);

  /// Parse various statement types.
  bool ParsePool(std::string* err);
  bool ParseRule(std::string* err);
  bool ParseLet(std::string* key, EvalString* val, std::string* err);
  bool ParseEdge(std::string* err);
  bool ParseDefault(std::string* err);

  /// Parse either a 'subninja' or 'include' line.
  bool ParseFileInclude(bool new_scope, std::string* err);

  /// If the next token is not \a expected, produce an error string
  /// saying "expectd foo, got bar".
  bool ExpectToken(Lexer::Token expected, std::string* err);

  State* state_;
  std::shared_ptr<BindingEnv> env_;
  FileReader* file_reader_;
  Lexer lexer_;
  ManifestParserOptions options_;
  bool quiet_;
};

}  // namespace ninja

#endif  // NINJA_MANIFEST_PARSER_H_
