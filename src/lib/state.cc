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

#include <assert.h>
#include <stdio.h>

#include <ninja/graph.h>
#include <ninja/metrics.h>
#include <ninja/util.h>

namespace ninja {

void Pool::EdgeScheduled(const Edge& edge) {
  if (depth_ != 0)
    current_use_ += edge.weight();
}

void Pool::EdgeFinished(const Edge& edge) {
  if (depth_ != 0)
    current_use_ -= edge.weight();
}

void Pool::DelayEdge(Edge* edge) {
  assert(depth_ != 0);
  delayed_.insert(edge);
}

void Pool::RetrieveReadyEdges(std::set<Edge*>* ready_queue) {
  DelayedEdges::iterator it = delayed_.begin();
  while (it != delayed_.end()) {
    Edge* edge = *it;
    if (current_use_ + edge->weight() > depth_)
      break;
    ready_queue->insert(edge);
    EdgeScheduled(*edge);
    ++it;
  }
  delayed_.erase(delayed_.begin(), it);
}

void Pool::Dump() const {
  printf("%s (%d/%d) ->\n", name_.c_str(), current_use_, depth_);
  for (DelayedEdges::const_iterator it = delayed_.begin(); it != delayed_.end();
       ++it) {
    printf("\t");
    (*it)->Dump();
  }
}

// static
bool Pool::WeightedEdgeCmp(const Edge* a, const Edge* b) {
  if (!a)
    return b;
  if (!b)
    return false;
  int weight_diff = a->weight() - b->weight();
  return ((weight_diff < 0) || (weight_diff == 0 && a < b));
}

State::State() {
  bindings_ = std::make_shared<BindingEnv>();
  auto phony_rule = std::make_unique<Rule>("phony");
  phony_rule_ = phony_rule.get();
  bindings_->AddRule(std::move(phony_rule));
  auto default_pool = std::make_unique<Pool>("", 0);
  default_pool_ = default_pool.get();
  AddPool(std::move(default_pool));
  AddPool(std::make_unique<Pool>("console", 1));
}

State::~State() = default;

void State::AddPool(std::unique_ptr<Pool> pool) {
  assert(LookupPool(pool->name()) == nullptr);
  pools_[pool->name()] = std::move(pool);
}

Pool* State::LookupPool(const std::string& pool_name) {
  auto i = pools_.find(pool_name);
  if (i == pools_.end())
    return nullptr;
  return i->second.get();
}

Edge* State::AddEdge(const Rule* rule) {
  auto owned_edge = std::make_unique<Edge>();
  auto edge = owned_edge.get();
  edge->rule_ = rule;
  edge->pool_ = default_pool_;
  edge->env_ = bindings_;
  edges_.push_back(std::move(owned_edge));
  return edge;
}

Node* State::GetNode(std::string_view path, uint64_t slash_bits) {
  if (Node* node = LookupNode(path))
    return node;

  auto owned_node = std::make_unique<Node>(std::string(path), slash_bits);
  Node* node = owned_node.get();
  paths_[node->path()] = std::move(owned_node);
  return node;
}

Node* State::LookupNode(std::string_view path) const {
  METRIC_RECORD("lookup node");
  Paths::const_iterator i = paths_.find(path);
  if (i != paths_.end())
    return i->second.get();
  return nullptr;
}

void State::AddIn(Edge* edge, std::string_view path, uint64_t slash_bits) {
  Node* node = GetNode(path, slash_bits);
  edge->inputs_.push_back(node);
  node->AddOutEdge(edge);
}

bool State::AddOut(Edge* edge, std::string_view path, uint64_t slash_bits) {
  Node* node = GetNode(path, slash_bits);
  if (node->in_edge())
    return false;
  edge->outputs_.push_back(node);
  node->set_in_edge(edge);
  return true;
}

bool State::AddDefault(std::string_view path, std::string* err) {
  Node* node = LookupNode(path);
  if (!node) {
    *err = "unknown target '" + std::string(path) + "'";
    return false;
  }
  defaults_.push_back(node);
  return true;
}

std::vector<Node*> State::RootNodes(std::string* err) const {
  std::vector<Node*> root_nodes;
  // Search for nodes with no output.
  for (const auto& e : edges_) {
    for (const auto& out : e->outputs_) {
      if (out->out_edges().empty())
        root_nodes.push_back(out);
    }
  }

  if (!edges_.empty() && root_nodes.empty())
    *err = "could not determine root nodes of build graph";

  return root_nodes;
}

std::vector<Node*> State::DefaultNodes(std::string* err) const {
  return defaults_.empty() ? RootNodes(err) : defaults_;
}

void State::Reset() {
  for (Paths::iterator i = paths_.begin(); i != paths_.end(); ++i)
    i->second->ResetState();
  for (const auto& e : edges_) {
    e->outputs_ready_ = false;
    e->mark_ = Edge::VisitNone;
  }
}

void State::Dump() {
  for (const auto& [path, node] : paths_) {
    (void)path;
    printf(
        "%s %s [id:%d]\n", node->path().c_str(),
        node->status_known() ? (node->dirty() ? "dirty" : "clean") : "unknown",
        node->id());
  }
  if (!pools_.empty()) {
    printf("resource_pools:\n");
    for (const auto& [name, pool] : pools_) {
      if (!name.empty()) {
        pool->Dump();
      }
    }
  }
}

}  // namespace ninja
