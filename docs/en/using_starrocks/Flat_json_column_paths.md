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
flat_json.column_paths.<json_column_name> = "<path1>, <path2>, ..."
```

- `<json_column_name>` is the identifier of a JSON column in the table schema.
  If the column does not exist or is not of JSON type, the DDL fails during analysis.
- Paths are comma-separated. Each path may start with `$.` (optional; stripped
  at parse time). Nested paths use `.` as separator: `$.user.country`.
- Setting the value to an empty string clears all force paths for that column.
- The forced paths share the same `flat_json.column.max` budget as the
  sparsity-derived columns. Forced paths consume the budget first (left-to-right
  in user-specified order); any leftover slots are filled by sparsity-derived
  columns. Excess paths beyond the budget are pushed to the raw remainder JSON
  blob, matching the existing `column.max` soft-cap behavior.

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
    "flat_json.column_paths.events"     = "$.browser, $.utm_source"
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

Use `ALTER TABLE ... SET (...)` to overwrite the complete path list for a column.

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

Multiple columns can be updated together:

```sql
ALTER TABLE logs SET (
    "flat_json.column_paths.request"  = "$.method, $.path, $.user_agent",
    "flat_json.column_paths.response" = "$.status"
);
```

## Interaction with sparsity

| Path state                  | Behavior                                                          |
|-----------------------------|-------------------------------------------------------------------|
| Listed in `column_paths`    | Forced to a sub-column regardless of sparsity (if it has `hits > 0`). |
| Listed but never appears    | Excluded (the engine does not create empty sub-columns).         |
| Not listed, above sparsity  | Auto-flattened via normal Flat JSON heuristic.                   |
| Not listed, below sparsity  | Stays inside the remainder JSON blob.                            |

Forced and auto-derived columns share the same `flat_json.column.max` budget.
Forced columns consume the budget first (left-to-right in user-specified order);
auto-derived columns fill any remaining slots. When more paths are forced than
the budget allows, the first paths in user-specified order are kept and the rest
fall back to the remainder JSON blob.

## Behavior with nested and irregular JSON

A forced path is a dot-separated path into the JSON document, so nested leaves
such as `$.address.city` are supported. The engine matches the path against the
actual document structure and degrades gracefully when the data does not match:

| Situation                                                            | Behavior                                                                                 |
|----------------------------------------------------------------------|------------------------------------------------------------------------------------------|
| Forced path points at a nested leaf (`$.a.b.c`)                       | The leaf is materialized as a sub-column.                                                |
| Forced path points at an object rather than a leaf (`$.a` = `{...}`)  | The object's child leaves are materialized.                                             |
| The forced leaf's ancestor is a scalar in some rows and an object in others | The conflicting node is materialized as one JSON sub-column; queries still return correct values. |
| Forced path is deeper than the data, or the key never appears        | No sub-column is created (no empty columns).                                             |
| Path is absent, `null`, `{}`, or the whole JSON value is `NULL` in a row | Handled safely and read back as `NULL`.                                                 |

Keys are matched **case-sensitively** and exactly, so `$.Browser` and
`$.browser` are different paths. Changing `column_paths` never rewrites existing
data or changes query results — it only affects the storage layout of segments
written afterward (and of existing data once it is compacted). Segments written
under different `column_paths` settings coexist, and queries return the same
results across all of them.

## Performance impact

Forcing a hot but sparse path into a typed sub-column avoids per-row JSON parsing
at read time and lets the planner push predicates down to storage. This trades a
small amount of extra work at load/compaction time for a large read-time gain on
paths that the sparsity heuristic would otherwise leave inside the remainder JSON
blob.

As an illustrative single-tablet measurement (300,000 rows, forced path present
in 25% of rows — below the sparsity threshold, so without `column_paths` it stays
in the remainder), queries that filter or extract the path ran several times
faster and the tablet was noticeably smaller than the same data with the path
left in the remainder. Actual numbers depend on data shape, value width, and
query pattern.

The benefit scales with the number of rows per tablet, so over-partitioning or
using too many buckets shrinks per-tablet row counts and dilutes the gain. Force
only genuinely hot paths: every forced sub-column consumes part of the
`flat_json.column.max` budget.

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

## Limitations (inherited from Flat JSON)

`flat_json.column_paths` builds on the same path-tree representation as the
base Flat JSON feature, so it inherits these limitations from
[Flat JSON](./Flat_json.md):

- **JSON Object keys only — JSON Array elements are not supported.** Paths
  like `$.items[0].name` or `$.users[1].email` cannot be materialized as
  sub-columns. If you put such a path in `flat_json.column_paths.<col>`,
  the path-tree treats `[N]` as part of a literal key name. The leaf
  receives zero hits and the path is silently skipped; in pathological
  cases (when array paths are the only forced paths) the whole JSON column
  may not be flattened at all. Queries via `get_json_*(col, '$.a[0].b')`
  still work, but they fall back to raw-JSON parsing without the
  sub-column speedup.
  - Workaround: redesign the JSON to use named keys
    (e.g., `{"first_item": {...}, "second_item": {...}}`).
- **Only leaf primitives are materialized.** If a path resolves to an
  intermediate JSON object (e.g., `forced = "a"` when the data is
  `{"a": {"b": 1, "c": 2}}`), only the leaf paths `a.b` and `a.c` are
  candidates for flattening — `a` itself is not stored as a sub-column.
  Specifying an intermediate path in `column_paths` therefore has no
  effect unless the data has `a` as a scalar in some rows.
- **Path tokens are dot-separated only.** Wildcards (`*`, `a.*`) and
  malformed forms (`a..b`, leading/trailing dots) are accepted by the
  property parser but silently produce no sub-column.

These limitations are not specific to this feature — they apply to the
entire StarRocks Flat JSON subsystem. See the
[Feature Limitations section in Flat JSON](./Flat_json.md#feature-limitations).

## Troubleshooting

| Symptom                                                    | Cause                                                                    | Fix                                                                        |
|------------------------------------------------------------|--------------------------------------------------------------------------|----------------------------------------------------------------------------|
| `Property 'flat_json.column_paths.foo' references unknown or non-JSON column 'foo'` | Column `foo` doesn't exist or isn't `JSON`.                              | Use the correct JSON column name; check `DESC <table>`.                    |
| `flat JSON configuration must be set after enabling flat JSON.` | Tried to set `column_paths` on a table with `flat_json.enable = false`. | `ALTER TABLE t SET ("flat_json.enable" = "true");` first.                  |
| `flat_json_meta` does not list a configured path           | The path does not appear in ANY row yet (`hits = 0`).                    | Load data containing the path, or wait until such rows arrive.             |
| `AccessPathHits = 0` but sub-column exists                 | Query reads old rowsets predating the config change.                     | Run `ALTER TABLE t COMPACT;` to re-flatten.                                |
| Configured paths exceed the quota                          | `flat_json.column.max` budget reached for that JSON column.              | Raise `flat_json.column.max`, or prune the list with a full-replace `ALTER`. |
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
