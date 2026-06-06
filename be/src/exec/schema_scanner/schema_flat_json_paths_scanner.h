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

#include <cstdint>
#include <string>
#include <vector>

#include "exec/schema_scanner.h"

namespace starrocks {

class SchemaFlatJsonPathsScanner : public SchemaScanner {
public:
    SchemaFlatJsonPathsScanner();
    ~SchemaFlatJsonPathsScanner() override;

    Status start(RuntimeState* state) override;
    Status get_next(ChunkPtr* chunk, bool* eos) override;

    struct FlatPathRow {
        int64_t table_id{0};
        int64_t tablet_id{0};
        std::string rowset_id;
        int32_t segment_id{0};
        std::string column_name;
        std::string path;
        std::string storage_type;
        std::string encoding;
    };

    // Test-only entry: inject pre-built rows without going through TabletManager
    // and segment-file I/O. Mirrors the TEST_start pattern used by other BE
    // schema scanners (see schema_fe_tablet_schedules_scanner_test.cpp).
    void TEST_start(int64_t be_id, std::vector<FlatPathRow> rows) {
        _be_id = be_id;
        _rows = std::move(rows);
        _cur_idx = 0;
    }

private:
    Status fill_chunk(ChunkPtr* chunk);

    int64_t _be_id{0};
    std::vector<FlatPathRow> _rows;
    size_t _cur_idx{0};
    static SchemaScanner::ColumnDesc _s_columns[];
};

} // namespace starrocks
