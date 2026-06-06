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
 * Schema-shape contract for {@code information_schema.flat_json_configs}.
 *
 * <p>The view exposes the FE-side flat_json configuration (enable, factors,
 * column_max) for any OlapTable that has a FlatJsonConfig set. Once a view
 * ships, its column count, names, and types become part of the public
 * compatibility surface — operators and dashboards SELECT specific columns,
 * so reordering, renaming, or changing a type is a breaking change.
 *
 * <p>These tests pin every column so that an accidental refactor of the
 * builder() chain in {@link FlatJsonConfigsSystemTable#create()} cannot
 * silently rotate columns or change a BOOLEAN to a TINYINT.
 */
public class FlatJsonConfigsSystemTableTest {

    @Test
    public void testNameAndId() {
        assertEquals("flat_json_configs", FlatJsonConfigsSystemTable.NAME);
        // SystemId is fixed once the view is in a released binary; rolling
        // back the value would re-map the table id of a deployed view.
        assertEquals(51L, SystemId.FLAT_JSON_CONFIGS_ID);
    }

    @Test
    public void testTableType() {
        SystemTable table = FlatJsonConfigsSystemTable.create();
        assertNotNull(table);
        assertEquals(Table.TableType.SCHEMA, table.getType());
    }

    @Test
    public void testColumnLayout() {
        SystemTable table = FlatJsonConfigsSystemTable.create();
        List<Column> cols = table.getFullSchema();

        // Pin both count and ordering — operators and BI tools SELECT positional
        // columns; ordering is part of the contract.
        assertEquals(8, cols.size(), "column count must remain 8 unless this is an intentional v2 view");

        assertEquals("TABLE_CATALOG", cols.get(0).getName());
        assertTrue(cols.get(0).getType().isStringType());

        assertEquals("TABLE_SCHEMA", cols.get(1).getName());
        assertTrue(cols.get(1).getType().isStringType());

        assertEquals("TABLE_NAME", cols.get(2).getName());
        assertTrue(cols.get(2).getType().isStringType());

        assertEquals("TABLE_ID", cols.get(3).getName());
        assertEquals(PrimitiveType.BIGINT, cols.get(3).getPrimitiveType());

        assertEquals("FLAT_JSON_ENABLE", cols.get(4).getName());
        assertEquals(PrimitiveType.BOOLEAN, cols.get(4).getPrimitiveType());

        assertEquals("FLAT_JSON_NULL_FACTOR", cols.get(5).getName());
        assertEquals(PrimitiveType.DOUBLE, cols.get(5).getPrimitiveType());

        assertEquals("FLAT_JSON_SPARSITY_FACTOR", cols.get(6).getName());
        assertEquals(PrimitiveType.DOUBLE, cols.get(6).getPrimitiveType());

        assertEquals("FLAT_JSON_COLUMN_MAX", cols.get(7).getName());
        assertEquals(PrimitiveType.INT, cols.get(7).getPrimitiveType());
    }
}
