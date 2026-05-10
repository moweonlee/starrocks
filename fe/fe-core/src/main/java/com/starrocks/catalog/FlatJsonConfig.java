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

package com.starrocks.catalog;

import com.google.gson.annotations.SerializedName;
import com.starrocks.common.Config;
import com.starrocks.common.io.Writable;
import com.starrocks.common.util.PropertyAnalyzer;
import com.starrocks.thrift.TFlatJsonConfig;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public class FlatJsonConfig implements Writable {
    @SerializedName("flatJsonEnable")
    private boolean flatJsonEnable;

    @SerializedName("flatJsonNullFactor")
    private double flatJsonNullFactor;

    @SerializedName("flatJsonSparsityFactor")
    private double flatJsonSparsityFactor;

    @SerializedName("flatJsonColumnMax")
    private int flatJsonColumnMax;

    // Per-JSON-column user-specified column paths: <json_column_name> -> list of dot-separated
    // paths (no leading "$."). Empty/absent entry means the column has no user-specified paths.
    @SerializedName("flatJsonColumnPaths")
    private Map<String, List<String>> flatJsonColumnPaths;

    // Per-column cap on user-specified column paths.
    // 0 = use all specified paths (bounded only by the system column limit). Default: 100.
    // When cap < path count, the first N paths in user-specified order are kept.
    @SerializedName("flatJsonColumnPathsMax")
    private int flatJsonColumnPathsMax;

    public FlatJsonConfig(boolean enabled, double nullFactor, double sparsityFactor, int columnMax) {
        this.flatJsonEnable = enabled;
        this.flatJsonNullFactor = nullFactor;
        this.flatJsonSparsityFactor = sparsityFactor;
        this.flatJsonColumnMax = columnMax;
        this.flatJsonColumnPaths = new LinkedHashMap<>();
        this.flatJsonColumnPathsMax = 0;
    }

    public FlatJsonConfig(FlatJsonConfig config) {
        this.flatJsonEnable = config.getFlatJsonEnable();
        this.flatJsonNullFactor = config.getFlatJsonNullFactor();
        this.flatJsonSparsityFactor = config.getFlatJsonSparsityFactor();
        this.flatJsonColumnMax = config.getFlatJsonColumnMax();
        this.flatJsonColumnPaths = deepCopyPaths(config.getFlatJsonColumnPaths());
        this.flatJsonColumnPathsMax = config.getFlatJsonColumnPathsMax();
    }

    public FlatJsonConfig() {
        this(false, Config.flat_json_null_factor, Config.flat_json_sparsity_factory,
                Config.flat_json_column_max);
    }

    public void buildFromProperties(Map<String, String> properties) {
        if (properties.containsKey(PropertyAnalyzer.PROPERTIES_FLAT_JSON_ENABLE)) {
            flatJsonEnable = Boolean.parseBoolean(properties.get(
                    PropertyAnalyzer.PROPERTIES_FLAT_JSON_ENABLE));
        }
        if (properties.containsKey(PropertyAnalyzer.PROPERTIES_FLAT_JSON_NULL_FACTOR)) {
            flatJsonNullFactor = Double.parseDouble(properties.get(
                    PropertyAnalyzer.PROPERTIES_FLAT_JSON_NULL_FACTOR));
        }
        if (properties.containsKey(PropertyAnalyzer.PROPERTIES_FLAT_JSON_SPARSITY_FACTOR)) {
            flatJsonSparsityFactor = Double.parseDouble(properties.get(
                    PropertyAnalyzer.PROPERTIES_FLAT_JSON_SPARSITY_FACTOR));
        }
        if (properties.containsKey(PropertyAnalyzer.PROPERTIES_FLAT_JSON_COLUMN_MAX)) {
            flatJsonColumnMax = Integer.parseInt(properties.get(
                    PropertyAnalyzer.PROPERTIES_FLAT_JSON_COLUMN_MAX));
        }
        // Replace (not merge) per-column paths from the properties map. This is the EditLog
        // replay path on follower FEs: leader emits the full state in toProperties(), so this
        // must overwrite any stale in-memory entries. Callers that want incremental ops
        // (.add/.remove) resolve them BEFORE serializing to properties.
        Map<String, List<String>> perCol = PropertyAnalyzer.analyzeFlatJsonColumnPaths(properties);
        flatJsonColumnPaths = new LinkedHashMap<>(perCol);
        if (properties.containsKey(PropertyAnalyzer.PROPERTIES_FLAT_JSON_COLUMN_PATHS_MAX)) {
            int max = PropertyAnalyzer.analyzeFlatJsonColumnPathsMax(properties);
            flatJsonColumnPathsMax = Math.max(max, 0);
        }
    }

    public boolean getFlatJsonEnable() {
        return flatJsonEnable;
    }

    public void setFlatJsonEnable(boolean flatJsonEnable) {
        this.flatJsonEnable = flatJsonEnable;
    }

    public double getFlatJsonNullFactor() {
        return flatJsonNullFactor;
    }

    public void setFlatJsonNullFactor(double flatJsonNullFactor) {
        this.flatJsonNullFactor = flatJsonNullFactor;
    }

    public double getFlatJsonSparsityFactor() {
        return flatJsonSparsityFactor;
    }

    public void setFlatJsonSparsityFactor(double flatJsonSparsityFactor) {
        this.flatJsonSparsityFactor = flatJsonSparsityFactor;
    }

    public int getFlatJsonColumnMax() {
        return flatJsonColumnMax;
    }

    public void setFlatJsonColumnMax(int flatJsonColumnMax) {
        this.flatJsonColumnMax = flatJsonColumnMax;
    }

    public Map<String, List<String>> getFlatJsonColumnPaths() {
        return flatJsonColumnPaths == null ? Collections.emptyMap() : flatJsonColumnPaths;
    }

    public void setFlatJsonColumnPaths(Map<String, List<String>> paths) {
        this.flatJsonColumnPaths = paths == null ? new LinkedHashMap<>() : new LinkedHashMap<>(paths);
    }

    // Returns the path list for a single JSON column (empty list if absent).
    public List<String> getColumnPaths(String columnName) {
        Map<String, List<String>> map = getFlatJsonColumnPaths();
        List<String> list = map.get(columnName);
        return list == null ? Collections.emptyList() : list;
    }

    // Replaces the path list for a single JSON column. Passing an empty/null list REMOVES
    // the entry so the column falls back to pure sparsity-based flattening.
    public void setColumnPaths(String columnName, List<String> paths) {
        if (flatJsonColumnPaths == null) {
            flatJsonColumnPaths = new LinkedHashMap<>();
        }
        if (paths == null || paths.isEmpty()) {
            flatJsonColumnPaths.remove(columnName);
        } else {
            flatJsonColumnPaths.put(columnName, new ArrayList<>(paths));
        }
    }

    public int getFlatJsonColumnPathsMax() {
        return flatJsonColumnPathsMax;
    }

    public void setFlatJsonColumnPathsMax(int max) {
        this.flatJsonColumnPathsMax = max;
    }

    // Serializes this config back into a flat properties map for EditLog persistence.
    //
    // CRITICAL: the EditLog replay path on follower FEs uses
    // TableProperty.modifyTableProperties(map) which does putAll() (merge, not replace) into
    // the existing properties map. If we conditionally omit keys for empty/zero values,
    // followers would retain stale values after a user removes all paths or resets _max to 0,
    // diverging from the leader's state. To keep leader and followers in sync we:
    //   1. Always emit the global _max key (so 0 reliably resets it).
    //   2. Emit one key per surviving column. Keys that previously existed but are now
    //      absent must be cleared on the follower; SchemaChangeHandler's ALTER path
    //      explicitly erases stale keys before calling toProperties() (see there for details).
    public Map<String, String> toProperties() {
        Map<String, String> properties = new HashMap<>();
        properties.put(PropertyAnalyzer.PROPERTIES_FLAT_JSON_ENABLE, String.valueOf(flatJsonEnable));
        if (flatJsonEnable) {
            properties.put(PropertyAnalyzer.PROPERTIES_FLAT_JSON_NULL_FACTOR, String.valueOf(flatJsonNullFactor));
            properties.put(PropertyAnalyzer.PROPERTIES_FLAT_JSON_SPARSITY_FACTOR,
                    String.valueOf(flatJsonSparsityFactor));
            properties.put(PropertyAnalyzer.PROPERTIES_FLAT_JSON_COLUMN_MAX, String.valueOf(flatJsonColumnMax));
            properties.put(PropertyAnalyzer.PROPERTIES_FLAT_JSON_COLUMN_PATHS_MAX,
                    String.valueOf(flatJsonColumnPathsMax));
            if (flatJsonColumnPaths != null) {
                for (Map.Entry<String, List<String>> entry : flatJsonColumnPaths.entrySet()) {
                    String key = PropertyAnalyzer.PROPERTIES_FLAT_JSON_COLUMN_PATHS_PREFIX + entry.getKey();
                    properties.put(key, String.join(",", entry.getValue()));
                }
            }
        }
        return properties;
    }

    public TFlatJsonConfig toTFlatJsonConfig() {
        TFlatJsonConfig tFlatJsonConfig = new TFlatJsonConfig();
        tFlatJsonConfig.setFlat_json_enable(flatJsonEnable);
        tFlatJsonConfig.setFlat_json_null_factor(flatJsonNullFactor);
        tFlatJsonConfig.setFlat_json_sparsity_factor(flatJsonSparsityFactor);
        tFlatJsonConfig.setFlat_json_column_max(flatJsonColumnMax);
        Map<String, List<String>> paths = getFlatJsonColumnPaths();
        if (!paths.isEmpty()) {
            tFlatJsonConfig.setFlat_json_column_paths(new TreeMap<>(paths));
        }
        // Always send column_paths_max so BE sees 0 as "use all specified paths".
        tFlatJsonConfig.setFlat_json_column_paths_max(flatJsonColumnPathsMax);
        return tFlatJsonConfig;
    }

    private static Map<String, List<String>> deepCopyPaths(Map<String, List<String>> src) {
        Map<String, List<String>> dst = new LinkedHashMap<>();
        if (src != null) {
            for (Map.Entry<String, List<String>> e : src.entrySet()) {
                dst.put(e.getKey(), new ArrayList<>(e.getValue()));
            }
        }
        return dst;
    }

    @Override
    public String toString() {
        return String.format("{ flat_json_enable : %b,\n " +
                "flat_json_null_factor : %f,\n " +
                "flat_json_sparsity_factor : %f,\n" +
                "flat_json_column_max : %d,\n" +
                "flat_json_column_paths : %s,\n" +
                "flat_json_column_paths_max : %d }",
                flatJsonEnable, flatJsonNullFactor, flatJsonSparsityFactor, flatJsonColumnMax,
                getFlatJsonColumnPaths(), flatJsonColumnPathsMax);
    }
}
