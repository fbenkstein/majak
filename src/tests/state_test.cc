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

#include <ninja/state.h>
#include <ninja/graph.h>

#include "test.h"

using namespace ninja;

namespace {

TEST(State, Basic) {
  State state;

  EvalString command;
  command.AddText("cat ");
  command.AddSpecial("in");
  command.AddText(" > ");
  command.AddSpecial("out");

  auto owned_rule = std::make_unique<Rule>("cat");
  auto rule = owned_rule.get();
  rule->AddBinding("command", command);
  state.bindings_->AddRule(std::move(owned_rule));

  Edge* edge = state.AddEdge(rule);
  state.AddIn(edge, "in1", 0);
  state.AddIn(edge, "in2", 0);
  state.AddOut(edge, "out", 0);

  EXPECT_EQ("cat in1 in2 > out", edge->EvaluateCommand());

  EXPECT_FALSE(state.GetNode("in1", 0)->dirty());
  EXPECT_FALSE(state.GetNode("in2", 0)->dirty());
  EXPECT_FALSE(state.GetNode("out", 0)->dirty());
}

}  // namespace
