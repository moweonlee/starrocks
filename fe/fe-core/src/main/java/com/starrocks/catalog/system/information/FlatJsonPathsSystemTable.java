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

import com.starrocks.catalog.Table;
import com.starrocks.catalog.system.SystemId;
import com.starrocks.catalog.system.SystemTable;
import com.starrocks.thrift.TSchemaTableType;
import com.starrocks.type.IntegerType;
import com.starrocks.type.TypeFactory;

import static com.starrocks.catalog.system.SystemTable.NAME_CHAR_LEN;
import static com.starrocks.catalog.system.SystemTable.builder;

public class FlatJsonPathsSystemTable {
    public static final String NAME = "flat_json_paths";

    public static SystemTable create() {
        return new SystemTable(SystemId.FLAT_JSON_PATHS_ID,
                NAME,
                Table.TableType.SCHEMA,
                builder()
                        .column("BE_ID", IntegerType.BIGINT)
                        .column("TABLE_ID", IntegerType.BIGINT)
                        .column("TABLET_ID", IntegerType.BIGINT)
                        .column("ROWSET_ID", TypeFactory.createVarcharType(NAME_CHAR_LEN))
                        .column("SEGMENT_ID", IntegerType.INT)
                        .column("COLUMN_NAME", TypeFactory.createVarcharType(NAME_CHAR_LEN))
                        .column("PATH", TypeFactory.createVarcharType(NAME_CHAR_LEN))
                        .column("STORAGE_TYPE", TypeFactory.createVarcharType(NAME_CHAR_LEN))
                        .column("ENCODING", TypeFactory.createVarcharType(NAME_CHAR_LEN))
                        .build(), TSchemaTableType.SCH_FLAT_JSON_PATHS);
    }
}
