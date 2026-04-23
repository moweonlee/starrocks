---
displayed_sidebar: docs
sidebar_position: 111
---

# Flat JSON Force-Flatten Paths (`column_paths`)

Flat JSON automatically extracts JSON fields that appear densely across rows (see
[Flat JSON](./Flat_json.md)). The sparsity heuristic works well for typical
payloads, but some fields are important for queries even when they appear in
only a small fraction of rows. `flat_json.column_paths` lets you explicitly
force-flatten a list of JSON paths for a specific JSON column, bypassing the
sparsity check.

This page describes the property syntax, how to set and update it, and how to
verify that the paths you specified were actually materialized as sub-columns.

## When to use

Use `column_paths` when a field is:

- **Queried frequently** but absent from most rows (below the sparsity threshold).
- **Used in predicates or joins** that benefit from columnar access.
- **Part of a low-cardinality dictionary** path that would otherwise be skipped.

Do NOT use it to force-flatten dense fields — those are picked up automatically
by the sparsity heuristic and consume the `flat_json.column.max` quota.

## Syntax

`column_paths` is scoped **per JSON column**. The property key encodes the JSON
column name:

```
flat_json.column_paths.<json_column_name>             = "<path1>, <path2>, ..."
flat_json.column_paths.<json_column_name>.add         = "<pathA>, <pathB>"       (ALTER only)
flat_json.column_paths.<json_column_name>.remove      = "<pathC>"                (ALTER only)
flat_json.column_paths_max                            = <int>                    (per-column cap)
```

- `<json_column_name>` is the identifier of a JSON column in the table schema.
  If the column does not exist or is not of JSON type, the DDL fails during analysis.
- Paths are comma-separated. Each path may start with `$.` (optional; stripped
  at parse time). Nested paths use `.` as separator: `$.user.country`.
- The reserved suffixes `.add` and `.remove` are only valid inside
  `ALTER TABLE ... SET (...)` — not at `CREATE TABLE` time.
- `flat_json.column_paths_max` caps the number of force-flattened columns **per
  JSON column**, independent of `flat_json.column.max` (which caps
  sparsity-derived columns). Default is `200`.

## Prerequisites

Flat JSON must be enabled on the table before any `column_paths` key is
accepted:

```sql
ALTER TABLE t SET ("flat_json.enable" = "true");
```

Attempting to set `column_paths` when Flat JSON is disabled raises a semantic
error.

## Configure at `CREATE TABLE` time

```sql
CREATE TABLE user_events (
    id      BIGINT,
    ts      DATETIME,
    events  JSON
)
DUPLICATE KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES (
    "flat_json.enable"                  = "true",
    "flat_json.null.factor"             = "0.3",
    "flat_json.sparsity.factor"         = "0.5",
    "flat_json.column.max"              = "50",
    -- Force-flatten events.browser and events.utm_source regardless of sparsity.
    "flat_json.column_paths.events"     = "$.browser, $.utm_source",
    -- Per-column cap on force paths (optional; default is 200).
    "flat_json.column_paths_max"        = "20"
);
```

Multiple JSON columns each get their own entry:

```sql
CREATE TABLE logs (
    id BIGINT,
    request  JSON,
    response JSON
)
DUPLICATE KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES (
    "flat_json.enable"                    = "true",
    "flat_json.column_paths.request"      = "$.method, $.path",
    "flat_json.column_paths.response"     = "$.status, $.latency_ms"
);
```

## Update via `ALTER TABLE`

Three operations are supported per JSON column. They can be combined in one
`ALTER` statement.

### Full replace

Overwrites the complete path list for a column.

```sql
ALTER TABLE user_events SET (
    "flat_json.column_paths.events" = "$.browser, $.country, $.utm_source"
);
```

Setting the value to an empty string clears all force paths for that column
(the column falls back to pure sparsity-based flattening):

```sql
ALTER TABLE user_events SET ("flat_json.column_paths.events" = "");
```

### Incremental add

Appends paths not already in the list. Existing paths are preserved.

```sql
ALTER TABLE user_events SET (
    "flat_json.column_paths.events.add" = "$.device_id, $.session_id"
);
```

### Incremental remove

Removes matching paths. Paths not in the current list are silently ignored.

```sql
ALTER TABLE user_events SET (
    "flat_json.column_paths.events.remove" = "$.utm_source"
);
```

### Global per-column cap

```sql
ALTER TABLE user_events SET ("flat_json.column_paths_max" = "50");
```

Setting it to `0` resets to the server default (`200`).

### Combined example

```sql
ALTER TABLE logs SET (
    "flat_json.column_paths.request.add"     = "$.user_agent",
    "flat_json.column_paths.response.remove" = "$.latency_ms",
    "flat_json.column_paths_max"             = "100"
);
```

## Interaction with sparsity

| Path state                  | Behavior                                                          |
|-----------------------------|-------------------------------------------------------------------|
| Listed in `column_paths`    | Forced to a sub-column regardless of sparsity (if it has `hits > 0`). |
| Listed but never appears    | Excluded (the engine does not create empty sub-columns).         |
| Not listed, above sparsity  | Auto-flattened via normal Flat JSON heuristic.                   |
| Not listed, below sparsity  | Stays inside the remainder JSON blob.                            |

The two quotas are independent:
- `flat_json.column.max` caps **auto-detected** sparse-flattened columns.
- `flat_json.column_paths_max` caps **forced** columns.

## Verification: step-by-step

Follow these steps to confirm that a configured path was actually materialized
as a sub-column.

### Step 1. Enable Flat JSON and configure paths

```sql
CREATE TABLE user_events (
    id BIGINT, ts DATETIME, events JSON
) DUPLICATE KEY (id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES (
    "flat_json.enable" = "true",
    "flat_json.sparsity.factor" = "0.9",
    "flat_json.column_paths.events" = "$.browser"
);
```

`sparsity.factor = 0.9` is set high to ensure that ordinary sparsity would NOT
flatten `$.browser`. This way, if `browser` shows up as a sub-column, it is
proof that `column_paths` took effect.

### Step 2. Load data where the forced path is sparse

```sql
INSERT INTO user_events VALUES
  (1, now(), parse_json('{"session":"a","browser":"Chrome"}')),
  (2, now(), parse_json('{"session":"b"}')),
  (3, now(), parse_json('{"session":"c"}')),
  (4, now(), parse_json('{"session":"d"}')),
  (5, now(), parse_json('{"session":"e"}'));
```

`browser` appears in 1/5 rows (sparsity 20%), well below the 90% threshold.
Without `column_paths` it would NOT be flattened.

### Step 3. Inspect materialized sub-columns

Use `flat_json_meta` on the `[_META_]` pseudo-table:

```sql
SELECT flat_json_meta(events) FROM user_events[_META_];
```

Expected output (keys in any order):

```
+-------------------------------------------------------+
| flat_json_meta(events)                                |
+-------------------------------------------------------+
| ["session(VARCHAR)", "browser(VARCHAR)"]              |
+-------------------------------------------------------+
```

Seeing `browser(VARCHAR)` in the list confirms the force-flatten happened.

If the table has multiple JSON columns, query each one individually:

```sql
SELECT flat_json_meta(request)  FROM logs[_META_];
SELECT flat_json_meta(response) FROM logs[_META_];
```

### Step 4. Verify query path usage via Query Profile

Run a query that touches the forced path:

```sql
SELECT get_json_string(events, '$.browser')
FROM user_events
WHERE get_json_string(events, '$.browser') = 'Chrome';
```

Then inspect the profile (enable with `SET enable_profile = true;` then
`SHOW PROFILELIST;` and `ANALYZE PROFILE FROM '<query_id>'`). Look for:

- `PushdownAccessPaths`: non-zero — the planner pushed the path access down to storage.
- `AccessPathHits`: non-zero — the sub-column was read directly instead of going through JSON.
- `/events: <n>` under `AccessPathHits` — your JSON column appears as a hit entry.

If `AccessPathHits` is zero but `AccessPathUnhits` is non-zero, the path was
pushed down but the sub-column did not exist for that rowset (typical right
after enabling the feature — new data is flattened but pre-existing segments
are not). Trigger compaction or reload the data to re-flatten historical rows.

### Step 5. Force compaction for existing data (optional)

New data is flattened at load time. For data that was already in the table
before you set `column_paths`, you can trigger compaction to re-flatten:

```sql
ALTER TABLE user_events COMPACT;
```

Watch the BE log for lines like:

```
Compaction flat json column: nulls(TINYINT),browser(VARCHAR),session(VARCHAR)
```

Repeat Step 3 to confirm `browser` is now present.

## Troubleshooting

| Symptom                                                    | Cause                                                                    | Fix                                                                        |
|------------------------------------------------------------|--------------------------------------------------------------------------|----------------------------------------------------------------------------|
| `Property 'flat_json.column_paths.foo' references unknown or non-JSON column 'foo'` | Column `foo` doesn't exist or isn't `JSON`.                              | Use the correct JSON column name; check `DESC <table>`.                    |
| `flat JSON configuration must be set after enabling flat JSON.` | Tried to set `column_paths` on a table with `flat_json.enable = false`. | `ALTER TABLE t SET ("flat_json.enable" = "true");` first.                  |
| `flat_json_meta` does not list a configured path           | The path does not appear in ANY row yet (`hits = 0`).                    | Load data containing the path, or wait until such rows arrive.             |
| `AccessPathHits = 0` but sub-column exists                 | Query reads old rowsets predating the config change.                     | Run `ALTER TABLE t COMPACT;` to re-flatten.                                |
| Configured paths exceed the quota                          | `flat_json.column_paths_max` reached for that JSON column.               | Raise the cap, or prune the list with `.remove`.                           |
| Sync appears wrong on follower FE after removing paths     | Older builds had an emit-only-if-non-empty bug.                          | This page's design emits the full state on every write (fixed).            |

## Metadata and cluster-wide consistency

`column_paths` configuration is persisted via the FE EditLog
(`OP_MODIFY_FLAT_JSON_CONFIG`). Every FE in the cluster replays the same log,
so the leader and followers converge to the same configuration. The config is
also pushed to every BE as part of the tablet meta and applied on the next
flush/compaction.

When debugging divergence, check:

```sql
-- View current effective config
SHOW CREATE TABLE user_events;
```

The `PROPERTIES` block will list every `flat_json.column_paths.<col>` entry
that is currently active. Compare this output between FE replicas; it must
match.

## Related

- [Flat JSON overview](./Flat_json.md)
- [CREATE TABLE reference](../sql-reference/sql-statements/table_bucket_part_index/CREATE_TABLE.md)
- [ALTER TABLE reference](../sql-reference/sql-statements/table_bucket_part_index/ALTER_TABLE.md)
- BE config: `json_flat_null_factor`, `json_flat_sparsity_factor`,
  `json_flat_column_max`, `enable_compaction_flat_json`
