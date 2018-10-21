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

#include <ninja/metrics.h>

#include <stdio.h>

#include <algorithm>
#include <vector>

namespace ninja {

Metrics* g_metrics = nullptr;

ScopedMetric::ScopedMetric(Metric* metric) {
  metric_ = metric;
  if (!metric_)
    return;
  start_ = std::chrono::high_resolution_clock::now();
}
ScopedMetric::~ScopedMetric() {
  if (!metric_)
    return;
  metric_->count++;
  int64_t dt = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::high_resolution_clock::now() - start_)
                   .count();
  metric_->sum += dt;
}

Metric* Metrics::NewMetric(const std::string& name) {
  Metric* metric = new Metric;
  metric->name = name;
  metric->count = 0;
  metric->sum = 0;
  metrics_.push_back(metric);
  return metric;
}

void Metrics::Report() {
  int width = 0;
  for (std::vector<Metric*>::iterator i = metrics_.begin(); i != metrics_.end();
       ++i) {
    width = std::max((int)(*i)->name.size(), width);
  }

  printf("%-*s\t%-6s\t%-9s\t%s\n", width, "metric", "count", "avg (us)",
         "total (ms)");
  for (std::vector<Metric*>::iterator i = metrics_.begin(); i != metrics_.end();
       ++i) {
    Metric* metric = *i;
    double total = metric->sum / (double)1000;
    double avg = metric->sum / (double)metric->count;
    printf("%-*s\t%-6d\t%-8.1f\t%.1f\n", width, metric->name.c_str(),
           metric->count, avg, total);
  }
}

int64_t GetTimeMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

}  // namespace ninja
