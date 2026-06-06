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

#include "common/system/master_info.h"
#include "exec/schema_scanner/schema_be_tablets_scanner.h"
#include "exec/schema_scanner/schema_helper.h"
#include "fs/fs.h"
#include "gen_cpp/segment.pb.h"
#include "runtime/exec_env.h"
#include "storage/rowset/rowset.h"
#include "storage/rowset/segment.h"
#include "storage/storage_engine.h"
#include "storage/tablet.h"
#include "storage/tablet_manager.h"
#include "storage/tablet_schema.h"
#include "types/logical_type.h"

namespace starrocks {

namespace {

std::set<int64_t> get_authorized_table_ids(const TGetTablesConfigResponse& resp) {
    std::set<int64_t> ids;
    for (const auto& v : resp.tables_config_infos) {
        if (v.__isset.table_id) {
            ids.insert(v.table_id);
        }
    }
    return ids;
}

const char* encoding_to_string(EncodingTypePB enc) {
    switch (enc) {
    case UNKNOWN_ENCODING:
        return "UNKNOWN";
    case DEFAULT_ENCODING:
        return "DEFAULT";
    case PLAIN_ENCODING:
        return "PLAIN";
    case PREFIX_ENCODING:
        return "PREFIX";
    case RLE:
        return "RLE";
    case DICT_ENCODING:
        return "DICT";
    case BIT_SHUFFLE:
        return "BIT_SHUFFLE";
    case FOR_ENCODING:
        return "FOR";
    default:
        return "UNKNOWN";
    }
}

// For a single segment, parse its footer and append all flat-JSON sub-column rows.
//
// Concurrency: we hold a RowsetSharedPtr (so the rowset metadata stays alive)
// but compaction between pick_all_candicate_rowsets() and this read can have
// already removed the on-disk segment file. The file_or check below treats
// that race as "row not visible in this scan", matching the view's
// snapshot-without-strict-consistency contract.
void scan_segment_footer(const RowsetSharedPtr& rowset, int32_t segment_id, int64_t table_id, int64_t tablet_id,
                         std::vector<SchemaFlatJsonPathsScanner::FlatPathRow>* out) {
    const std::string seg_path = Rowset::segment_file_path(rowset->rowset_path(), rowset->rowset_id(), segment_id);

    auto file_or = FileSystem::Default()->new_random_access_file(seg_path);
    if (!file_or.ok()) {
        VLOG(2) << "flat_json_paths: failed to open " << seg_path << ": " << file_or.status();
        return;
    }
    auto& file = file_or.value();

    SegmentFooterPB footer;
    auto footer_or = Segment::parse_segment_footer(file.get(), &footer, nullptr, nullptr);
    if (!footer_or.ok()) {
        VLOG(2) << "flat_json_paths: failed to parse footer of " << seg_path << ": " << footer_or.status();
        return;
    }

    const TabletSchemaCSPtr& schema = rowset->schema();

    for (const auto& col_meta : footer.columns()) {
        if (!col_meta.has_json_meta() || !col_meta.json_meta().is_flat()) {
            continue;
        }

        // Resolve parent JSON column name via TabletSchema using column_id (ordinal in schema).
        std::string parent_name;
        if (col_meta.has_column_id() && col_meta.column_id() < schema->num_columns()) {
            parent_name = std::string(schema->column(col_meta.column_id()).name());
        }

        for (const auto& child : col_meta.children_columns()) {
            SchemaFlatJsonPathsScanner::FlatPathRow row;
            row.table_id = table_id;
            row.tablet_id = tablet_id;
            row.rowset_id = rowset->rowset_id().to_string();
            row.segment_id = segment_id;
            row.column_name = parent_name;
            row.path = child.has_name() ? child.name() : std::string{};
            const auto lt = static_cast<LogicalType>(child.type());
            row.storage_type = logical_type_to_string(lt);
            row.encoding = encoding_to_string(child.encoding());
            out->push_back(std::move(row));
        }
    }
}

} // namespace

SchemaScanner::ColumnDesc SchemaFlatJsonPathsScanner::_s_columns[] = {
        {"BE_ID", TypeDescriptor::from_logical_type(TYPE_BIGINT), sizeof(int64_t), false},
        {"TABLE_ID", TypeDescriptor::from_logical_type(TYPE_BIGINT), sizeof(int64_t), false},
        {"TABLET_ID", TypeDescriptor::from_logical_type(TYPE_BIGINT), sizeof(int64_t), false},
        {"ROWSET_ID", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"SEGMENT_ID", TypeDescriptor::from_logical_type(TYPE_INT), sizeof(int32_t), false},
        {"COLUMN_NAME", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"PATH", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"STORAGE_TYPE", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
        {"ENCODING", TypeDescriptor::create_varchar_type(sizeof(Slice)), sizeof(Slice), false},
};

SchemaFlatJsonPathsScanner::SchemaFlatJsonPathsScanner()
        : SchemaScanner(_s_columns, sizeof(_s_columns) / sizeof(SchemaScanner::ColumnDesc)) {}

SchemaFlatJsonPathsScanner::~SchemaFlatJsonPathsScanner() = default;

Status SchemaFlatJsonPathsScanner::start(RuntimeState* state) {
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

    TGetTablesConfigRequest tables_config_req;
    tables_config_req.__set_auth_info(auth_info);

    RETURN_IF_ERROR(SchemaScanner::init_schema_scanner_state(state));

    TGetTablesConfigResponse tables_config_resp;
    RETURN_IF_ERROR(SchemaHelper::get_tables_config(_ss_state, tables_config_req, &tables_config_resp));
    auto authorized_table_ids = get_authorized_table_ids(tables_config_resp);

    auto o_id = get_backend_id();
    _be_id = o_id.has_value() ? o_id.value() : -1;
    _rows.clear();

    auto* manager = StorageEngine::instance()->tablet_manager();
    if (manager == nullptr) {
        // shared-data only: nothing to enumerate locally for this MVP
        _cur_idx = 0;
        return Status::OK();
    }

    std::vector<TabletBasicInfo> infos;
    manager->get_tablets_basic_infos(_param->table_id, _param->partition_id, _param->tablet_id, infos,
                                     &authorized_table_ids);

    for (const auto& info : infos) {
        auto tablet = manager->get_tablet(info.tablet_id);
        if (tablet == nullptr) {
            continue;
        }
        std::vector<RowsetSharedPtr> rowsets;
        tablet->pick_all_candicate_rowsets(&rowsets);
        for (const auto& rowset : rowsets) {
            if (rowset == nullptr) {
                continue;
            }
            const int64_t num_seg = rowset->num_segments();
            for (int32_t seg_id = 0; seg_id < num_seg; ++seg_id) {
                scan_segment_footer(rowset, seg_id, info.table_id, info.tablet_id, &_rows);
            }
        }
    }

    _cur_idx = 0;
    return Status::OK();
}

Status SchemaFlatJsonPathsScanner::fill_chunk(ChunkPtr* chunk) {
    const auto& slot_id_to_index_map = (*chunk)->get_slot_id_to_index_map();
    auto end = _cur_idx + 1;
    for (; _cur_idx < end; ++_cur_idx) {
        auto& r = _rows[_cur_idx];
        for (const auto& [slot_id, index] : slot_id_to_index_map) {
            if (slot_id < 1 || slot_id > 9) {
                return Status::InternalError("invalid slot id for flat_json_paths");
            }
            auto* column = (*chunk)->get_column_raw_ptr_by_slot_id(slot_id);
            switch (slot_id) {
            case 1:
                fill_column_with_slot<TYPE_BIGINT>(column, (void*)&_be_id);
                break;
            case 2:
                fill_column_with_slot<TYPE_BIGINT>(column, (void*)&r.table_id);
                break;
            case 3:
                fill_column_with_slot<TYPE_BIGINT>(column, (void*)&r.tablet_id);
                break;
            case 4: {
                Slice s(r.rowset_id);
                fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&s);
                break;
            }
            case 5:
                fill_column_with_slot<TYPE_INT>(column, (void*)&r.segment_id);
                break;
            case 6: {
                Slice s(r.column_name);
                fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&s);
                break;
            }
            case 7: {
                Slice s(r.path);
                fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&s);
                break;
            }
            case 8: {
                Slice s(r.storage_type);
                fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&s);
                break;
            }
            case 9: {
                Slice s(r.encoding);
                fill_column_with_slot<TYPE_VARCHAR>(column, (void*)&s);
                break;
            }
            default:
                break;
            }
        }
    }
    return Status::OK();
}

Status SchemaFlatJsonPathsScanner::get_next(ChunkPtr* chunk, bool* eos) {
    if (!_is_init) {
        return Status::InternalError("call this before initial.");
    }
    if (_cur_idx >= _rows.size()) {
        *eos = true;
        return Status::OK();
    }
    if (chunk == nullptr || eos == nullptr) {
        return Status::InternalError("invalid parameter");
    }
    *eos = false;
    return fill_chunk(chunk);
}

} // namespace starrocks
