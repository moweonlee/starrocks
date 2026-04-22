// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <gen_cpp/olap_file.pb.h>

#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gen_cpp/AgentService_types.h"

namespace starrocks {
class FlatJsonConfig {
public:
    // Default max force-path columns (per JSON column) when not specified by the caller.
    static constexpr int DEFAULT_COLUMN_PATHS_MAX = 200;

    using ColumnPathsMap = std::unordered_map<std::string, std::unordered_set<std::string>>;

    // Constructor
    FlatJsonConfig();

    // Constructor with parameters
    FlatJsonConfig(bool enable, double nullFactor, double sparsityFactor, int maxColumnMax)
            : _flat_json_enable(enable),
              _flat_json_null_factor(nullFactor),
              _flat_json_sparsity_factor(sparsityFactor),
              _flat_json_max_column_max(maxColumnMax),
              _flat_json_column_paths_max(DEFAULT_COLUMN_PATHS_MAX) {}

    // Getters and Setters
    bool is_flat_json_enabled() const { return _flat_json_enable; }
    void set_flat_json_enabled(bool enable) { _flat_json_enable = enable; }

    double get_flat_json_null_factor() const { return _flat_json_null_factor; }
    void set_flat_json_null_factor(double factor) { _flat_json_null_factor = factor; }

    double get_flat_json_sparsity_factor() const { return _flat_json_sparsity_factor; }
    void set_flat_json_sparsity_factor(double factor) { _flat_json_sparsity_factor = factor; }

    int get_flat_json_max_column_max() const { return _flat_json_max_column_max; }
    void set_flat_json_max_column_max(int max) { _flat_json_max_column_max = max; }

    // Per-JSON-column force-flatten paths (dot-separated, no leading "$.").
    const ColumnPathsMap& get_column_paths_map() const { return _flat_json_column_paths; }

    // Returns the path set for the given JSON column, or nullptr if the column has no forced paths.
    const std::unordered_set<std::string>* get_column_paths_for(const std::string& column_name) const {
        auto it = _flat_json_column_paths.find(column_name);
        return it == _flat_json_column_paths.end() ? nullptr : &it->second;
    }

    void set_column_paths_map(ColumnPathsMap paths) { _flat_json_column_paths = std::move(paths); }

    void set_column_paths(const std::string& column_name, const std::vector<std::string>& paths) {
        std::unordered_set<std::string> s;
        for (const auto& p : paths) {
            s.insert(p);
        }
        _flat_json_column_paths[column_name] = std::move(s);
    }

    int get_column_paths_max() const { return _flat_json_column_paths_max; }
    void set_column_paths_max(int max) { _flat_json_column_paths_max = max; }

    void to_pb(FlatJsonConfigPB* binlog_config_pb) {
        binlog_config_pb->set_flat_json_enable(_flat_json_enable);
        binlog_config_pb->set_flat_json_null_factor(_flat_json_null_factor);
        binlog_config_pb->set_flat_json_sparsity_factor(_flat_json_sparsity_factor);
        binlog_config_pb->set_flat_json_max_column_max(_flat_json_max_column_max);
        binlog_config_pb->clear_flat_json_column_paths();
        for (const auto& [col, paths] : _flat_json_column_paths) {
            auto* entry = binlog_config_pb->add_flat_json_column_paths();
            entry->set_column_name(col);
            for (const auto& p : paths) {
                entry->add_paths(p);
            }
        }
        binlog_config_pb->set_flat_json_column_paths_max(_flat_json_column_paths_max);
    }

    // Update function using another FlatJsonConfig
    void update(const FlatJsonConfig& config) {
        _flat_json_enable = config.is_flat_json_enabled();
        _flat_json_null_factor = config.get_flat_json_null_factor();
        _flat_json_sparsity_factor = config.get_flat_json_sparsity_factor();
        _flat_json_max_column_max = config.get_flat_json_max_column_max();
        _flat_json_column_paths = config.get_column_paths_map();
        _flat_json_column_paths_max = config.get_column_paths_max();
    }

    void update(const TFlatJsonConfig& config) {
        _flat_json_enable = config.flat_json_enable;
        _flat_json_null_factor = config.flat_json_null_factor;
        _flat_json_sparsity_factor = config.flat_json_sparsity_factor;
        _flat_json_max_column_max = config.flat_json_column_max;
        _flat_json_column_paths.clear();
        if (config.__isset.flat_json_column_paths) {
            for (const auto& [col, paths] : config.flat_json_column_paths) {
                std::unordered_set<std::string> s(paths.begin(), paths.end());
                _flat_json_column_paths.emplace(col, std::move(s));
            }
        }
        if (config.__isset.flat_json_column_paths_max && config.flat_json_column_paths_max > 0) {
            _flat_json_column_paths_max = static_cast<int>(config.flat_json_column_paths_max);
        }
    }

    void update(const FlatJsonConfigPB& flat_json_config_pb) {
        _flat_json_enable = flat_json_config_pb.flat_json_enable();
        _flat_json_null_factor = flat_json_config_pb.flat_json_null_factor();
        _flat_json_sparsity_factor = flat_json_config_pb.flat_json_sparsity_factor();
        _flat_json_max_column_max = flat_json_config_pb.flat_json_max_column_max();
        _flat_json_column_paths.clear();
        for (const auto& entry : flat_json_config_pb.flat_json_column_paths()) {
            std::unordered_set<std::string> s;
            for (const auto& p : entry.paths()) {
                s.insert(p);
            }
            _flat_json_column_paths.emplace(entry.column_name(), std::move(s));
        }
        if (flat_json_config_pb.has_flat_json_column_paths_max() &&
            flat_json_config_pb.flat_json_column_paths_max() > 0) {
            _flat_json_column_paths_max = static_cast<int>(flat_json_config_pb.flat_json_column_paths_max());
        }
    }

    // Update function using four parameters (kept for backward compatibility with original API)
    void update(bool enable, double nullFactor, double sparsityFactor, int maxColumnMax) {
        _flat_json_enable = enable;
        _flat_json_null_factor = nullFactor;
        _flat_json_sparsity_factor = sparsityFactor;
        _flat_json_max_column_max = maxColumnMax;
    }

    // Copy Assignment
    FlatJsonConfig& operator=(const FlatJsonConfig& other) {
        if (this != &other) {
            _flat_json_enable = other._flat_json_enable;
            _flat_json_null_factor = other._flat_json_null_factor;
            _flat_json_sparsity_factor = other._flat_json_sparsity_factor;
            _flat_json_max_column_max = other._flat_json_max_column_max;
            _flat_json_column_paths = other._flat_json_column_paths;
            _flat_json_column_paths_max = other._flat_json_column_paths_max;
        }
        return *this;
    }

    std::string to_string() const {
        std::ostringstream oss;
        oss << "FlatJsonConfig{";
        oss << "flat_json_enable=" << (_flat_json_enable ? "true" : "false") << ", ";
        oss << "flat_json_null_factor=" << _flat_json_null_factor << ", ";
        oss << "flat_json_sparsity_factor=" << _flat_json_sparsity_factor << ", ";
        oss << "flat_json_max_column_max=" << _flat_json_max_column_max << ", ";
        oss << "flat_json_column_paths={";
        bool first_col = true;
        for (const auto& [col, paths] : _flat_json_column_paths) {
            if (!first_col) oss << ",";
            oss << col << ":[";
            bool first_p = true;
            for (const auto& p : paths) {
                if (!first_p) oss << ",";
                oss << p;
                first_p = false;
            }
            oss << "]";
            first_col = false;
        }
        oss << "}, ";
        oss << "flat_json_column_paths_max=" << _flat_json_column_paths_max;
        oss << "}";
        return oss.str();
    }

private:
    bool _flat_json_enable = false;
    double _flat_json_null_factor = 0;
    double _flat_json_sparsity_factor = 0;
    int _flat_json_max_column_max = 0;
    // Per-JSON-column force-flatten paths: column_name -> set of dot-separated paths (no leading "$.").
    ColumnPathsMap _flat_json_column_paths;
    int _flat_json_column_paths_max = DEFAULT_COLUMN_PATHS_MAX;
};
} // namespace starrocks
