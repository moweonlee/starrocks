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

#include "exec/schema_scanner/schema_flat_json_configs_scanner.h"

#include "common/logging.h"
#include "exec/schema_scanner/schema_helper.h"
#include "types/logical_type.h"

namespace starrocks {

SchemaScanner::ColumnDesc SchemaFlatJsonConfigsScanner::_s_columns[] = {
        //   name,                       type,                                                 size,            is_null
        {"TABLE_CATALOG", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"TABLE_SCHEMA", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"TABLE_NAME", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"TABLE_ID", TypeDescriptor::from_logical_type(TYPE_BIGINT), sizeof(int64_t), false},
        {"FLAT_JSON_ENABLE", TypeDescriptor::from_logical_type(TYPE_BOOLEAN), sizeof(bool), false},
        {"FLAT_JSON_NULL_FACTOR", TypeDescriptor::from_logical_type(TYPE_DOUBLE), sizeof(double), false},
        {"FLAT_JSON_SPARSITY_FACTOR", TypeDescriptor::from_logical_type(TYPE_DOUBLE), sizeof(double), false},
        {"FLAT_JSON_COLUMN_MAX", TypeDescriptor::from_logical_type(TYPE_INT), sizeof(int32_t), false},
};

SchemaFlatJsonConfigsScanner::SchemaFlatJsonConfigsScanner()
        : SchemaScanner(_s_columns, sizeof(_s_columns) / sizeof(SchemaScanner::ColumnDesc)) {}

SchemaFlatJsonConfigsScanner::~SchemaFlatJsonConfigsScanner() = default;

Status SchemaFlatJsonConfigsScanner::start(RuntimeState* state) {
    if (!_is_init) {
        return Status::InternalError("used before initialized.");
    }
    TAuthInfo auth_info;
    if (nullptr != _param->db) {
        auth_info.__set_pattern(*(_param->db));
    }
    if (nullptr != _param->current_user_ident) {
        auth_info.__set_current_user_ident(*(_param->current_user_ident));
    } else {
        if (nullptr != _param->user) {
            auth_info.__set_user(*(_param->user));
        }
        if (nullptr != _param->user_ip) {
            auth_info.__set_user_ip(*(_param->user_ip));
        }
    }
    TGetFlatJsonConfigsRequest req;
    req.__set_auth_info(auth_info);
    if (nullptr != _param->table) {
        req.__set_table_name(*(_param->table));
    }

    RETURN_IF_ERROR(SchemaScanner::init_schema_scanner_state(state));
    RETURN_IF_ERROR(SchemaHelper::get_flat_json_configs(_ss_state, req, &_response));
    return Status::OK();
}

Status SchemaFlatJsonConfigsScanner::get_next(ChunkPtr* chunk, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("Used before initialized.");
    }
    if (nullptr == chunk || nullptr == eos) {
        return Status::InternalError("input pointer is nullptr.");
    }
    if (_index >= _response.flat_json_configs_infos.size()) {
        *eos = true;
        return Status::OK();
    }
    *eos = false;
    return fill_chunk(chunk);
}

Status SchemaFlatJsonConfigsScanner::fill_chunk(ChunkPtr* chunk) {
    const TFlatJsonConfigInfo& info = _response.flat_json_configs_infos[_index];
    const auto& slot_id_to_index_map = (*chunk)->get_slot_id_to_index_map();
    for (const auto& [slot_id, index] : slot_id_to_index_map) {
        switch (slot_id) {
        case 1: {
            // TABLE_CATALOG
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(1);
            Slice value(info.table_catalog.c_str(), info.table_catalog.length());
            fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&value);
            break;
        }
        case 2: {
            // TABLE_SCHEMA
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(2);
            Slice value(info.table_schema.c_str(), info.table_schema.length());
            fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&value);
            break;
        }
        case 3: {
            // TABLE_NAME
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(3);
            Slice value(info.table_name.c_str(), info.table_name.length());
            fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&value);
            break;
        }
        case 4: {
            // TABLE_ID
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(4);
            fill_column_with_slot<TYPE_BIGINT>(column, (void*)&info.table_id);
            break;
        }
        case 5: {
            // FLAT_JSON_ENABLE
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(5);
            bool v = info.flat_json_enable;
            fill_column_with_slot<TYPE_BOOLEAN>(column, (void*)&v);
            break;
        }
        case 6: {
            // FLAT_JSON_NULL_FACTOR
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(6);
            fill_column_with_slot<TYPE_DOUBLE>(column, (void*)&info.flat_json_null_factor);
            break;
        }
        case 7: {
            // FLAT_JSON_SPARSITY_FACTOR
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(7);
            fill_column_with_slot<TYPE_DOUBLE>(column, (void*)&info.flat_json_sparsity_factor);
            break;
        }
        case 8: {
            // FLAT_JSON_COLUMN_MAX
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(8);
            fill_column_with_slot<TYPE_INT>(column, (void*)&info.flat_json_column_max);
            break;
        }
        default:
            break;
        }
    }
    _index++;
    return Status::OK();
}

} // namespace starrocks
