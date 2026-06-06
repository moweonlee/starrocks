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

#include "exec/schema_scanner/schema_flat_json_paths_scanner.h"

#include <gtest/gtest.h>

#include "base/testutil/assert.h"
#include "column/column_helper.h"
#include "runtime/runtime_state.h"

namespace starrocks {

class SchemaFlatJsonPathsScannerTest : public ::testing::Test {
protected:
    ChunkPtr create_chunk(const std::vector<SlotDescriptor*>& slot_descs) {
        ChunkPtr chunk = std::make_shared<Chunk>();
        for (const auto* slot_desc : slot_descs) {
            MutableColumnPtr column = ColumnHelper::create_column(slot_desc->type(), slot_desc->is_nullable());
            chunk->append_column(std::move(column), slot_desc->id());
        }
        return chunk;
    }

    SchemaFlatJsonPathsScanner::FlatPathRow make_row(int64_t table_id, int64_t tablet_id, std::string rowset_id,
                                                     int32_t segment_id, std::string column_name, std::string path,
                                                     std::string storage_type, std::string encoding) {
        SchemaFlatJsonPathsScanner::FlatPathRow r;
        r.table_id = table_id;
        r.tablet_id = tablet_id;
        r.rowset_id = std::move(rowset_id);
        r.segment_id = segment_id;
        r.column_name = std::move(column_name);
        r.path = std::move(path);
        r.storage_type = std::move(storage_type);
        r.encoding = std::move(encoding);
        return r;
    }

    // Initialize scanner so _is_init becomes true (init() resolves slot descs
    // against TupleDescriptor — the upstream pattern; we call it indirectly
    // via SchemaScannerParam with a constructed TupleDescriptor).
    void init_scanner(SchemaFlatJsonPathsScanner* scanner, SchemaScannerParam* params, ObjectPool* pool) {
        ASSERT_OK(scanner->init(params, pool));
    }
};

// Smoke test: scanner reports exactly 9 columns with the documented schema.
// The slot_id switch in start()/fill_chunk() is keyed by 1..9 — a column
// count change without updating that switch would silently mis-route values.
TEST_F(SchemaFlatJsonPathsScannerTest, test_column_layout) {
    SchemaFlatJsonPathsScanner scanner;
    SchemaScannerParam params;
    ObjectPool pool;
    init_scanner(&scanner, &params, &pool);

    const auto& slots = scanner.get_slot_descs();
    ASSERT_EQ(9, slots.size());
    EXPECT_EQ("BE_ID", slots[0]->col_name());
    EXPECT_EQ("TABLE_ID", slots[1]->col_name());
    EXPECT_EQ("TABLET_ID", slots[2]->col_name());
    EXPECT_EQ("ROWSET_ID", slots[3]->col_name());
    EXPECT_EQ("SEGMENT_ID", slots[4]->col_name());
    EXPECT_EQ("COLUMN_NAME", slots[5]->col_name());
    EXPECT_EQ("PATH", slots[6]->col_name());
    EXPECT_EQ("STORAGE_TYPE", slots[7]->col_name());
    EXPECT_EQ("ENCODING", slots[8]->col_name());
}

// End-to-end through fill_chunk: emit two rows representing a single
// segment with two flat sub-columns, and verify the chunk gets them
// in the right slot order with the right values.
TEST_F(SchemaFlatJsonPathsScannerTest, test_fill_chunk_normal) {
    SchemaFlatJsonPathsScanner scanner;
    SchemaScannerParam params;
    ObjectPool pool;
    init_scanner(&scanner, &params, &pool);

    std::vector<SchemaFlatJsonPathsScanner::FlatPathRow> rows;
    rows.push_back(make_row(/*table*/ 1000, /*tablet*/ 2000, /*rowset*/ "rs-abc", /*seg*/ 0,
                            /*col*/ "j", /*path*/ "event.browser", /*type*/ "VARCHAR", /*enc*/ "DICT"));
    rows.push_back(make_row(/*table*/ 1000, /*tablet*/ 2000, /*rowset*/ "rs-abc", /*seg*/ 0,
                            /*col*/ "j", /*path*/ "remain", /*type*/ "JSON", /*enc*/ "PLAIN"));
    scanner.TEST_start(/*be_id*/ 9999, std::move(rows));

    auto chunk = create_chunk(scanner.get_slot_descs());
    bool eos = false;

    ASSERT_OK(scanner.get_next(&chunk, &eos));
    ASSERT_FALSE(eos);
    EXPECT_EQ(1, chunk->num_rows());
    // BE_ID=9999, TABLE_ID=1000, TABLET_ID=2000, ROWSET_ID='rs-abc', SEGMENT_ID=0,
    // COLUMN_NAME='j', PATH='event.browser', STORAGE_TYPE='VARCHAR', ENCODING='DICT'
    EXPECT_EQ("[9999, 1000, 2000, 'rs-abc', 0, 'j', 'event.browser', 'VARCHAR', 'DICT']", chunk->debug_row(0));

    chunk->reset();
    ASSERT_OK(scanner.get_next(&chunk, &eos));
    ASSERT_FALSE(eos);
    EXPECT_EQ(1, chunk->num_rows());
    EXPECT_EQ("[9999, 1000, 2000, 'rs-abc', 0, 'j', 'remain', 'JSON', 'PLAIN']", chunk->debug_row(0));

    chunk->reset();
    ASSERT_OK(scanner.get_next(&chunk, &eos));
    EXPECT_TRUE(eos);
}

// Empty injection: no rows -> first get_next sets eos=true, no rows emitted.
// This pins the contract that a tablet with no flat sub-columns (e.g. a
// table with flat_json.enable=false, all segments raw JSON) produces an
// empty result for that tablet rather than dummy rows.
TEST_F(SchemaFlatJsonPathsScannerTest, test_empty_rows_produce_eos) {
    SchemaFlatJsonPathsScanner scanner;
    SchemaScannerParam params;
    ObjectPool pool;
    init_scanner(&scanner, &params, &pool);

    scanner.TEST_start(/*be_id*/ 9999, {});

    auto chunk = create_chunk(scanner.get_slot_descs());
    bool eos = false;
    ASSERT_OK(scanner.get_next(&chunk, &eos));
    EXPECT_TRUE(eos);
    EXPECT_EQ(0, chunk->num_rows());
}

// Cross-BE separation: two rows from different BE_IDs (only one BE worth
// makes it through a single scanner instance because TEST_start sets one
// _be_id). The view's cluster-wide BE_ID values come from each BE's own
// scanner instance — this test pins the per-instance contract.
TEST_F(SchemaFlatJsonPathsScannerTest, test_be_id_threaded_through) {
    SchemaFlatJsonPathsScanner scanner;
    SchemaScannerParam params;
    ObjectPool pool;
    init_scanner(&scanner, &params, &pool);

    std::vector<SchemaFlatJsonPathsScanner::FlatPathRow> rows;
    rows.push_back(make_row(7, 7, "rs-x", 0, "j", "a", "BIGINT", "BIT_SHUFFLE"));
    scanner.TEST_start(/*be_id*/ 42, std::move(rows));

    auto chunk = create_chunk(scanner.get_slot_descs());
    bool eos = false;
    ASSERT_OK(scanner.get_next(&chunk, &eos));
    EXPECT_FALSE(eos);
    EXPECT_EQ("[42, 7, 7, 'rs-x', 0, 'j', 'a', 'BIGINT', 'BIT_SHUFFLE']", chunk->debug_row(0));
}

} // namespace starrocks
