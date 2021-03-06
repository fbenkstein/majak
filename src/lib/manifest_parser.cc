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

#include <ninja/manifest_parser.h>

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <ninja/disk_interface.h>
#include <ninja/graph.h>
#include <ninja/metrics.h>
#include <ninja/state.h>
#include <ninja/util.h>
#include <ninja/version.h>

namespace ninja {

ManifestParser::ManifestParser(State* state, FileReader* file_reader,
                               ManifestParserOptions options)
    : state_(state), file_reader_(file_reader), options_(options),
      quiet_(false) {
  env_ = state->bindings_;
}

bool ManifestParser::Load(const std::string& filename, std::string* err,
                          Lexer* parent) {
  METRIC_RECORD(".ninja parse");
  std::string contents;
  std::string read_err;
  if (file_reader_->ReadFile(filename, &contents, &read_err) !=
      FileReader::Okay) {
    *err = "loading '" + filename + "': " + read_err;
    if (parent)
      parent->Error(std::string(*err), err);
    return false;
  }

  // The lexer needs a nul byte at the end of its input, to know when it's done.
  // It takes a std::string_view, and std::string_view's string constructor uses
  // string::data().  data()'s return value isn't guaranteed to be
  // null-terminated (although in practice - libc++, libstdc++, msvc's stl --
  // it is, and C++11 demands that too), so add an explicit nul byte.
  contents.resize(contents.size() + 1);

  return Parse(filename, contents, err);
}

bool ManifestParser::Parse(const std::string& filename,
                           const std::string& input, std::string* err) {
  lexer_.Start(filename, input);

  for (;;) {
    Lexer::Token token = lexer_.ReadToken();
    switch (token) {
    case Lexer::POOL:
      if (!ParsePool(err))
        return false;
      break;
    case Lexer::BUILD:
      if (!ParseEdge(err))
        return false;
      break;
    case Lexer::RULE:
      if (!ParseRule(err))
        return false;
      break;
    case Lexer::DEFAULT:
      if (!ParseDefault(err))
        return false;
      break;
    case Lexer::IDENT: {
      lexer_.UnreadToken();
      std::string name;
      EvalString let_value;
      if (!ParseLet(&name, &let_value, err))
        return false;
      std::string value = let_value.Evaluate(env_.get());
      // Check ninja_required_version immediately so we can exit
      // before encountering any syntactic surprises.
      if (name == "ninja_required_version")
        CheckNinjaVersion(value);
      env_->AddBinding(name, value);
      break;
    }
    case Lexer::INCLUDE:
      if (!ParseFileInclude(false, err))
        return false;
      break;
    case Lexer::SUBNINJA:
      if (!ParseFileInclude(true, err))
        return false;
      break;
    case Lexer::ERROR: {
      return lexer_.Error(lexer_.DescribeLastError(), err);
    }
    case Lexer::TEOF:
      return true;
    case Lexer::NEWLINE:
      break;
    default:
      return lexer_.Error(std::string("unexpected ") + Lexer::TokenName(token),
                          err);
    }
  }
  return false;  // not reached
}

bool ManifestParser::ParsePool(std::string* err) {
  std::string name;
  if (!lexer_.ReadIdent(&name))
    return lexer_.Error("expected pool name", err);

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  if (state_->LookupPool(name) != nullptr)
    return lexer_.Error("duplicate pool '" + name + "'", err);

  int depth = -1;

  while (lexer_.PeekToken(Lexer::INDENT)) {
    std::string key;
    EvalString value;
    if (!ParseLet(&key, &value, err))
      return false;

    if (key == "depth") {
      std::string depth_string = value.Evaluate(env_.get());
      depth = atol(depth_string.c_str());
      if (depth < 0)
        return lexer_.Error("invalid pool depth", err);
    } else {
      return lexer_.Error("unexpected variable '" + key + "'", err);
    }
  }

  if (depth < 0)
    return lexer_.Error("expected 'depth =' line", err);

  state_->AddPool(std::make_unique<Pool>(name, depth));
  return true;
}

bool ManifestParser::ParseRule(std::string* err) {
  std::string name;
  if (!lexer_.ReadIdent(&name))
    return lexer_.Error("expected rule name", err);

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  if (env_->LookupRuleCurrentScope(name) != nullptr)
    return lexer_.Error("duplicate rule '" + name + "'", err);

  auto rule = std::make_unique<Rule>(name);

  while (lexer_.PeekToken(Lexer::INDENT)) {
    std::string key;
    EvalString value;
    if (!ParseLet(&key, &value, err))
      return false;

    if (Rule::IsReservedBinding(key)) {
      rule->AddBinding(key, value);
    } else {
      // Die on other keyvals for now; revisit if we want to add a
      // scope here.
      return lexer_.Error("unexpected variable '" + key + "'", err);
    }
  }

  if (rule->bindings_["rspfile"].empty() !=
      rule->bindings_["rspfile_content"].empty()) {
    return lexer_.Error(
        "rspfile and rspfile_content need to be "
        "both specified",
        err);
  }

  if (rule->bindings_["command"].empty())
    return lexer_.Error("expected 'command =' line", err);

  env_->AddRule(std::move(rule));
  return true;
}

bool ManifestParser::ParseLet(std::string* key, EvalString* value,
                              std::string* err) {
  if (!lexer_.ReadIdent(key))
    return lexer_.Error("expected variable name", err);
  if (!ExpectToken(Lexer::EQUALS, err))
    return false;
  if (!lexer_.ReadVarValue(value, err))
    return false;
  return true;
}

bool ManifestParser::ParseDefault(std::string* err) {
  EvalString eval;
  if (!lexer_.ReadPath(&eval, err))
    return false;
  if (eval.empty())
    return lexer_.Error("expected target name", err);

  do {
    std::string path = eval.Evaluate(env_.get());
    std::string path_err;
    uint64_t slash_bits;  // Unused because this only does lookup.
    if (!CanonicalizePath(&path, &slash_bits, &path_err))
      return lexer_.Error(path_err, err);
    if (!state_->AddDefault(path, &path_err))
      return lexer_.Error(path_err, err);

    eval.Clear();
    if (!lexer_.ReadPath(&eval, err))
      return false;
  } while (!eval.empty());

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  return true;
}

bool ManifestParser::ParseEdge(std::string* err) {
  std::vector<EvalString> ins, outs;

  {
    EvalString out;
    if (!lexer_.ReadPath(&out, err))
      return false;
    while (!out.empty()) {
      outs.push_back(out);

      out.Clear();
      if (!lexer_.ReadPath(&out, err))
        return false;
    }
  }

  // Add all implicit outs, counting how many as we go.
  int implicit_outs = 0;
  if (lexer_.PeekToken(Lexer::PIPE)) {
    for (;;) {
      EvalString out;
      if (!lexer_.ReadPath(&out, err))
        return err;
      if (out.empty())
        break;
      outs.push_back(out);
      ++implicit_outs;
    }
  }

  if (outs.empty())
    return lexer_.Error("expected path", err);

  if (!ExpectToken(Lexer::COLON, err))
    return false;

  std::string rule_name;
  if (!lexer_.ReadIdent(&rule_name))
    return lexer_.Error("expected build command name", err);

  const Rule* rule = env_->LookupRule(rule_name);
  if (!rule)
    return lexer_.Error("unknown build rule '" + rule_name + "'", err);

  for (;;) {
    // XXX should we require one path here?
    EvalString in;
    if (!lexer_.ReadPath(&in, err))
      return false;
    if (in.empty())
      break;
    ins.push_back(in);
  }

  // Add all implicit deps, counting how many as we go.
  int implicit = 0;
  if (lexer_.PeekToken(Lexer::PIPE)) {
    for (;;) {
      EvalString in;
      if (!lexer_.ReadPath(&in, err))
        return err;
      if (in.empty())
        break;
      ins.push_back(in);
      ++implicit;
    }
  }

  // Add all order-only deps, counting how many as we go.
  int order_only = 0;
  if (lexer_.PeekToken(Lexer::PIPE2)) {
    for (;;) {
      EvalString in;
      if (!lexer_.ReadPath(&in, err))
        return false;
      if (in.empty())
        break;
      ins.push_back(in);
      ++order_only;
    }
  }

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  // Bindings on edges are rare, so allocate per-edge envs only when needed.
  bool has_indent_token = lexer_.PeekToken(Lexer::INDENT);
  std::shared_ptr<BindingEnv> env =
      has_indent_token ? std::make_shared<BindingEnv>(env_) : env_;
  while (has_indent_token) {
    std::string key;
    EvalString val;
    if (!ParseLet(&key, &val, err))
      return false;

    env->AddBinding(key, val.Evaluate(env_.get()));
    has_indent_token = lexer_.PeekToken(Lexer::INDENT);
  }

  Edge* edge = state_->AddEdge(rule);
  edge->env_ = env;

  std::string pool_name = edge->GetBinding("pool");
  if (!pool_name.empty()) {
    Pool* pool = state_->LookupPool(pool_name);
    if (pool == nullptr)
      return lexer_.Error("unknown pool name '" + pool_name + "'", err);
    edge->pool_ = pool;
  }

  edge->outputs_.reserve(outs.size());
  for (size_t i = 0, e = outs.size(); i != e; ++i) {
    std::string path = outs[i].Evaluate(env.get());
    std::string path_err;
    uint64_t slash_bits;
    if (!CanonicalizePath(&path, &slash_bits, &path_err))
      return lexer_.Error(path_err, err);
    if (!state_->AddOut(edge, path, slash_bits)) {
      if (options_.dupe_edge_action_ == kDupeEdgeActionError) {
        lexer_.Error("multiple rules generate " + path + " [-w dupbuild=err]",
                     err);
        return false;
      } else {
        if (!quiet_) {
          Warning(
              "multiple rules generate %s. "
              "builds involving this target will not be correct; "
              "continuing anyway [-w dupbuild=warn]",
              path.c_str());
        }
        if (e - i <= static_cast<size_t>(implicit_outs))
          --implicit_outs;
      }
    }
  }
  if (edge->outputs_.empty()) {
    // All outputs of the edge are already created by other edges. Don't add
    // this edge.  Do this check before input nodes are connected to the edge.
    state_->edges_.pop_back();
    return true;
  }
  edge->implicit_outs_ = implicit_outs;

  edge->inputs_.reserve(ins.size());
  for (std::vector<EvalString>::iterator i = ins.begin(); i != ins.end(); ++i) {
    std::string path = i->Evaluate(env.get());
    std::string path_err;
    uint64_t slash_bits;
    if (!CanonicalizePath(&path, &slash_bits, &path_err))
      return lexer_.Error(path_err, err);
    state_->AddIn(edge, path, slash_bits);
  }
  edge->implicit_deps_ = implicit;
  edge->order_only_deps_ = order_only;

  if (options_.phony_cycle_action_ == kPhonyCycleActionWarn &&
      edge->maybe_phonycycle_diagnostic()) {
    // CMake 2.8.12.x and 3.0.x incorrectly write phony build statements
    // that reference themselves.  Ninja used to tolerate these in the
    // build graph but that has since been fixed.  Filter them out to
    // support users of those old CMake versions.
    Node* out = edge->outputs_[0];
    std::vector<Node*>::iterator new_end =
        remove(edge->inputs_.begin(), edge->inputs_.end(), out);
    if (new_end != edge->inputs_.end()) {
      edge->inputs_.erase(new_end, edge->inputs_.end());
      if (!quiet_) {
        Warning(
            "phony target '%s' names itself as an input; "
            "ignoring [-w phonycycle=warn]",
            out->path().c_str());
      }
    }
  }

  // Multiple outputs aren't (yet?) supported with depslog.
  std::string deps_type = edge->GetBinding("deps");
  if (!deps_type.empty() && edge->outputs_.size() > 1) {
    return lexer_.Error(
        "multiple outputs aren't (yet?) supported by depslog; "
        "bring this up on the mailing list if it affects you",
        err);
  }

  return true;
}

bool ManifestParser::ParseFileInclude(bool new_scope, std::string* err) {
  EvalString eval;
  if (!lexer_.ReadPath(&eval, err))
    return false;
  std::string path = eval.Evaluate(env_.get());

  ManifestParser subparser(state_, file_reader_, options_);
  if (new_scope) {
    subparser.env_ = std::make_shared<BindingEnv>(env_);
  } else {
    subparser.env_ = env_;
  }

  if (!subparser.Load(path, err, &lexer_))
    return false;

  if (!ExpectToken(Lexer::NEWLINE, err))
    return false;

  return true;
}

bool ManifestParser::ExpectToken(Lexer::Token expected, std::string* err) {
  Lexer::Token token = lexer_.ReadToken();
  if (token != expected) {
    std::string message = std::string("expected ") + Lexer::TokenName(expected);
    message += std::string(", got ") + Lexer::TokenName(token);
    message += Lexer::TokenErrorHint(expected);
    return lexer_.Error(message, err);
  }
  return true;
}

}  // namespace ninja
