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

package com.starrocks.catalog.system.information;

import com.starrocks.catalog.Column;
import com.starrocks.catalog.Table;
import com.starrocks.catalog.system.SystemId;
import com.starrocks.catalog.system.SystemTable;
import com.starrocks.type.PrimitiveType;
import org.junit.jupiter.api.Test;

import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Schema-shape contract for {@code information_schema.flat_json_paths}.
 *
 * <p>Unlike flat_json_configs (FE-only), the row data of this view comes
 * from a BE-local scanner walking the segment footers. The FE side is just
 * the column declaration; if a column gets dropped or renamed here without
 * a corresponding BE change, the chunk fill would mis-align by slot id and
 * write the wrong value into the wrong column at query time.
 *
 * <p>The slot_id switch in
 * {@code be/src/exec/schema_scanner/schema_flat_json_paths_scanner.cpp}
 * is keyed by 1-based column ordinal. The asserts below pin that ordering.
 */
public class FlatJsonPathsSystemTableTest {

    @Test
    public void testNameAndId() {
        assertEquals("flat_json_paths", FlatJsonPathsSystemTable.NAME);
        assertEquals(52L, SystemId.FLAT_JSON_PATHS_ID);
    }

    @Test
    public void testTableType() {
        SystemTable table = FlatJsonPathsSystemTable.create();
        assertNotNull(table);
        assertEquals(Table.TableType.SCHEMA, table.getType());
    }

    @Test
    public void testColumnLayout() {
        SystemTable table = FlatJsonPathsSystemTable.create();
        List<Column> cols = table.getFullSchema();

        // The BE scanner fill_chunk() switches on 1..9 slot ids; the FE
        // declaration MUST keep this exact ordering or the chunk emits
        // values into wrong columns at the FE.
        assertEquals(9, cols.size(), "column count must remain 9 unless this is an intentional v2 view");

        assertEquals("BE_ID", cols.get(0).getName());
        assertEquals(PrimitiveType.BIGINT, cols.get(0).getPrimitiveType());

        assertEquals("TABLE_ID", cols.get(1).getName());
        assertEquals(PrimitiveType.BIGINT, cols.get(1).getPrimitiveType());

        assertEquals("TABLET_ID", cols.get(2).getName());
        assertEquals(PrimitiveType.BIGINT, cols.get(2).getPrimitiveType());

        assertEquals("ROWSET_ID", cols.get(3).getName());
        assertTrue(cols.get(3).getType().isStringType());

        assertEquals("SEGMENT_ID", cols.get(4).getName());
        assertEquals(PrimitiveType.INT, cols.get(4).getPrimitiveType());

        assertEquals("COLUMN_NAME", cols.get(5).getName());
        assertTrue(cols.get(5).getType().isStringType());

        assertEquals("PATH", cols.get(6).getName());
        assertTrue(cols.get(6).getType().isStringType());

        assertEquals("STORAGE_TYPE", cols.get(7).getName());
        assertTrue(cols.get(7).getType().isStringType());

        assertEquals("ENCODING", cols.get(8).getName());
        assertTrue(cols.get(8).getType().isStringType());
    }

    @Test
    public void testIdsAreDistinctFromConfigsView() {
        // The two flat-json views must have different system ids; reusing
        // 51L for both would clash in InfoSchemaDb's registration.
        assertEquals(false, SystemId.FLAT_JSON_PATHS_ID == SystemId.FLAT_JSON_CONFIGS_ID);
    }
}
