---
displayed_sidebar: docs
sidebar_position: 111
---

# Flat JSON 강제 평탄화 경로 (`column_paths`)

Flat JSON은 여러 행에 걸쳐 빈번하게 등장하는 JSON 필드를 자동으로 별도 컬럼으로
추출합니다 ([Flat JSON](./Flat_json.md) 참조). 희소성(sparsity) 휴리스틱은 대부분의
페이로드에 잘 동작하지만, 일부 필드는 소수의 행에만 등장해도 쿼리 성능에 중요할 수
있습니다. `flat_json.column_paths`는 **특정 JSON 컬럼**에 대해 지정한 경로들을 희소성
체크를 무시하고 **강제로 평탄화**하도록 설정하는 기능입니다.

이 문서는 해당 프로퍼티의 문법, 설정/변경 방법, 그리고 **지정한 경로가 실제로
서브컬럼으로 materialize 되었는지 검증하는 단계별 방법**을 다룹니다.

## 언제 사용하나

다음과 같은 필드에 사용하세요:

- **자주 쿼리**되지만 **대부분의 행에는 없는** 필드 (희소성 임계값 미만).
- **조건절 또는 조인**에 쓰여서 컬럼 액세스로 이득을 보는 필드.
- 저-카디널리티 dictionary 최적화 대상 필드인데 희소성 때문에 스킵될 가능성이 있는 경우.

**사용하지 말아야 할 경우:** 대부분의 행에 등장하는 dense 필드. 이런 필드는 기존
휴리스틱이 자동으로 평탄화하며, `flat_json.column.max` 쿼터를 소비합니다.

## 문법

`column_paths`는 **JSON 컬럼별**로 독립적으로 지정됩니다. 프로퍼티 키에 JSON 컬럼명을
박아 스코프를 구분합니다:

```
flat_json.column_paths.<json_컬럼명> = "<경로1>, <경로2>, ..."
```

- `<json_컬럼명>`은 테이블 스키마에 존재하는 JSON 타입 컬럼이어야 합니다. 존재하지 않거나
  JSON 타입이 아니면 DDL analyze 단계에서 실패합니다.
- 경로는 쉼표로 구분합니다. 각 경로는 `$.` 접두사로 시작할 수 있으며(선택, 파싱 시 제거),
  중첩 경로는 `.`으로 구분합니다: `$.user.country`.
- 값을 빈 문자열로 설정하면 해당 컬럼의 강제 경로가 모두 제거됩니다.
- 강제 경로는 `flat_json.column.max` budget 을 희소성 기반 컬럼과 함께 공유합니다.
  강제 경로가 사용자 지정 순서대로 좌→우로 budget 을 먼저 소비하고, 남은 slot 에
  희소성 기반 컬럼이 채워집니다. budget 을 초과한 경로는 raw remainder JSON 으로
  강등됩니다 (기존 `column.max` 의 soft-cap 동작과 동일).

## 전제 조건

`column_paths`를 설정하려면 먼저 Flat JSON이 활성화되어 있어야 합니다:

```sql
ALTER TABLE t SET ("flat_json.enable" = "true");
```

비활성 상태에서 `column_paths` 관련 키를 설정하면 semantic 에러가 발생합니다.

## CREATE TABLE 시점에 설정

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
    -- events.browser, events.utm_source을 희소성 무시하고 강제 평탄화
    "flat_json.column_paths.events"     = "$.browser, $.utm_source"
);
```

여러 JSON 컬럼이 있으면 각각 별도 키를 사용합니다:

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

## ALTER TABLE로 변경

`ALTER TABLE ... SET (...)`으로 해당 컬럼의 경로 리스트를 완전히 새 값으로 덮어씁니다.

```sql
ALTER TABLE user_events SET (
    "flat_json.column_paths.events" = "$.browser, $.country, $.utm_source"
);
```

빈 문자열을 주면 해당 컬럼의 강제 경로를 모두 제거합니다 (이후 그 컬럼은 순수하게
희소성 기반으로만 평탄화됩니다):

```sql
ALTER TABLE user_events SET ("flat_json.column_paths.events" = "");
```

여러 컬럼을 한 ALTER 에서 같이 갱신할 수 있습니다:

```sql
ALTER TABLE logs SET (
    "flat_json.column_paths.request"  = "$.method, $.path, $.user_agent",
    "flat_json.column_paths.response" = "$.status"
);
```

## 희소성과의 상호작용

| 경로 상태                               | 동작                                                              |
|-----------------------------------------|-------------------------------------------------------------------|
| `column_paths`에 포함                   | 희소성 무시하고 서브컬럼으로 만듦 (단, `hits > 0`이어야 함).       |
| 포함됐지만 데이터에 전혀 등장하지 않음  | 제외 (빈 서브컬럼은 생성되지 않음).                               |
| 미포함, 희소성 임계치 이상              | 일반 Flat JSON 휴리스틱으로 자동 평탄화.                          |
| 미포함, 희소성 임계치 미만              | 잔여(remainder) JSON blob에 유지.                                 |

강제 경로와 자동 감지 경로는 같은 `flat_json.column.max` budget 을 공유합니다.
강제 경로가 사용자 지정 순서대로 좌→우로 budget 을 먼저 소비하고, 남은 slot 에
자동 감지된 컬럼이 채워집니다.

## 검증: 단계별 가이드

지정한 경로가 실제로 서브컬럼으로 materialize 되었는지 다음 단계로 확인합니다.

### Step 1. Flat JSON 활성화 + 경로 지정

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

`sparsity.factor = 0.9`로 높게 설정해서, 일반 희소성 로직으로는 `$.browser`가 절대
평탄화되지 않게 합니다. 이렇게 하면 `browser`가 서브컬럼으로 나타난다는 것은 곧
`column_paths`가 동작했다는 증거가 됩니다.

### Step 2. 해당 경로가 희소하게 나타나는 데이터 적재

```sql
INSERT INTO user_events VALUES
  (1, now(), parse_json('{"session":"a","browser":"Chrome"}')),
  (2, now(), parse_json('{"session":"b"}')),
  (3, now(), parse_json('{"session":"c"}')),
  (4, now(), parse_json('{"session":"d"}')),
  (5, now(), parse_json('{"session":"e"}'));
```

`browser`는 1/5 (20%)에만 등장하므로 90% 임계치에 크게 못 미칩니다. 즉
`column_paths`가 없었다면 평탄화되지 **않을** 데이터입니다.

### Step 3. Materialize 된 서브컬럼 확인

`[_META_]` 가상 테이블에 `flat_json_meta` 함수를 사용합니다:

```sql
SELECT flat_json_meta(events) FROM user_events[_META_];
```

예상 결과 (키 순서는 상관없음):

```
+-------------------------------------------------------+
| flat_json_meta(events)                                |
+-------------------------------------------------------+
| ["session(VARCHAR)", "browser(VARCHAR)"]              |
+-------------------------------------------------------+
```

**목록에 `browser(VARCHAR)`가 보이면 강제 평탄화가 적용된 것입니다.**

테이블에 JSON 컬럼이 여러 개라면 각각 조회:

```sql
SELECT flat_json_meta(request)  FROM logs[_META_];
SELECT flat_json_meta(response) FROM logs[_META_];
```

### Step 4. Query Profile로 서브컬럼 사용 확인

강제 경로를 건드리는 쿼리 실행:

```sql
SELECT get_json_string(events, '$.browser')
FROM user_events
WHERE get_json_string(events, '$.browser') = 'Chrome';
```

프로파일 확인 (`SET enable_profile = true;` → `SHOW PROFILELIST;` →
`ANALYZE PROFILE FROM '<query_id>'`). 다음 지표를 보세요:

- `PushdownAccessPaths`: 0보다 크면 플래너가 경로 접근을 스토리지로 push down한 것.
- `AccessPathHits`: 0보다 크면 서브컬럼을 직접 읽어 JSON 파싱 없이 값 접근 성공.
- `/events: <n>` 항목이 `AccessPathHits` 아래에 보임 — 해당 JSON 컬럼이 hit 되었음.

`AccessPathHits = 0` 인데 `AccessPathUnhits > 0` 이면, 플래너는 push down했지만 해당
rowset에 서브컬럼이 없는 상태입니다 (기능 활성화 직후에 흔함 — 신규 데이터는
평탄화되지만 기존 세그먼트는 그대로). 컴팩션을 트리거하거나 데이터를 다시 적재해서
재평탄화하세요.

### Step 5. 기존 데이터에 대한 강제 컴팩션 (선택)

신규 데이터는 로드 시점에 평탄화됩니다. `column_paths` 설정 이전의 데이터에 대해서는
컴팩션을 트리거해서 재평탄화할 수 있습니다:

```sql
ALTER TABLE user_events COMPACT;
```

BE 로그에서 다음과 같은 줄을 확인:

```
Compaction flat json column: nulls(TINYINT),browser(VARCHAR),session(VARCHAR)
```

그 후 Step 3을 반복해서 `browser`가 나타나는지 확인합니다.

## 제약 사항 (Flat JSON 으로부터 상속)

`flat_json.column_paths` 는 기본 Flat JSON 의 path tree 구조를 그대로 활용하므로,
[Flat JSON](./Flat_json.md) 의 제약을 동일하게 상속합니다:

- **JSON Object key 만 지원 — JSON Array element 는 미지원**.
  `$.items[0].name`, `$.users[1].email` 같은 경로는 sub-column 으로 materialize 안 됨.
  `flat_json.column_paths.<col>` 에 이런 경로를 넣으면 path tree 가 `[N]` 을 literal key
  이름의 일부로 처리합니다. leaf 가 hit=0 이 되어 silent skip; 극단적으로 forced 경로가
  array path 만 있으면 JSON column 전체가 flat 안 되는 경우도 발생.
  query `get_json_*(col, '$.a[0].b')` 는 여전히 동작하지만 raw JSON 파싱 fallback
  으로 sub-column 가속 없음.
  - 우회: JSON 구조를 named key 로 변경 (예: `{"first_item": {...}, "second_item": {...}}`).
- **Leaf primitive 만 materialize**. 경로가 intermediate JSON object 를 가리키면
  (예: 데이터가 `{"a": {"b": 1, "c": 2}}` 인데 `forced = "a"`), leaf 인 `a.b`, `a.c`
  만 flatten 후보. `a` 자체는 sub-column 으로 저장 안 됨. intermediate path 를
  `column_paths` 에 지정해도 효과 없음 (데이터에 `a` 가 scalar 인 row 가 없는 한).
- **Path 토큰은 점 (`.`) 구분만 지원**. Wildcard (`*`, `a.*`) 와 malformed 형식
  (`a..b`, 시작/끝의 점) 은 property parser 가 accept 하지만 sub-column 으로
  변환되지 않음 (silent no-op).

이 제약들은 본 기능 한정이 아니라 StarRocks Flat JSON 전체에 적용됩니다.
자세한 내용은 [Flat JSON 의 Feature Limitations](./Flat_json.md#feature-limitations) 참조.

## 트러블슈팅

| 증상                                                          | 원인                                                                   | 조치                                                                       |
|---------------------------------------------------------------|------------------------------------------------------------------------|----------------------------------------------------------------------------|
| `Property 'flat_json.column_paths.foo' references unknown or non-JSON column 'foo'` | `foo` 컬럼이 없거나 JSON 타입이 아님.                                   | 올바른 JSON 컬럼명 사용. `DESC <table>`로 확인.                            |
| `flat JSON configuration must be set after enabling flat JSON.` | Flat JSON이 비활성 상태에서 `column_paths` 설정 시도.                  | 먼저 `ALTER TABLE t SET ("flat_json.enable" = "true");`.                   |
| `flat_json_meta`에 설정한 경로가 안 보임                      | 아직 해당 경로가 어느 행에도 등장하지 않음 (`hits = 0`).                | 해당 경로를 포함한 데이터를 적재하거나 등장할 때까지 대기.                 |
| 서브컬럼은 존재하지만 `AccessPathHits = 0`                    | 쿼리가 설정 변경 이전의 기존 rowset을 읽고 있음.                       | `ALTER TABLE t COMPACT;`로 재평탄화.                                       |
| 지정한 경로 수가 쿼터 초과                                    | 해당 JSON 컬럼의 `flat_json.column.max` budget 도달.                    | `flat_json.column.max` 를 올리거나 ALTER full-replace 로 경로 수를 줄임.    |
| Remove 후 follower FE의 상태가 이상함                         | 초기 버전의 emit-only-if-non-empty 버그.                                | 본 설계에서는 항상 전체 상태를 emit하여 수정됨.                            |

## 메타데이터와 클러스터 전체 일관성

`column_paths` 설정은 FE EditLog(`OP_MODIFY_FLAT_JSON_CONFIG`)로 영속화됩니다. 모든
FE가 같은 로그를 replay하므로 리더와 팔로워가 동일한 설정으로 수렴합니다. 설정은
tablet meta의 일부로 모든 BE에도 전파되며, 다음 flush/compaction 시점에 반영됩니다.

디버깅 시 확인 방법:

```sql
-- 현재 유효한 설정 확인
SHOW CREATE TABLE user_events;
```

`PROPERTIES` 블록에 현재 활성화된 `flat_json.column_paths.<col>` 항목들이 표시됩니다.
FE replica들 간에 이 출력을 비교해서 동일한지 확인하세요.

## 관련 문서

- [Flat JSON 개요](./Flat_json.md)
- [CREATE TABLE 레퍼런스](../sql-reference/sql-statements/table_bucket_part_index/CREATE_TABLE.md)
- [ALTER TABLE 레퍼런스](../sql-reference/sql-statements/table_bucket_part_index/ALTER_TABLE.md)
- BE 파라미터: `json_flat_null_factor`, `json_flat_sparsity_factor`,
  `json_flat_column_max`, `enable_compaction_flat_json`
