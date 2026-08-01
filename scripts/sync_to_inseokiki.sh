#!/usr/bin/env bash
# sync_to_inseokiki.sh — phy_lab/personal(기준본) -> inseokiki(적용본) 단방향 동기화
#
# 사용법:
#   sync_to_inseokiki.sh                 # dry-run (기본값, 아무것도 쓰지 않음)
#   sync_to_inseokiki.sh --apply         # 실제 복사 수행
#   sync_to_inseokiki.sh --dest=<경로>   # 대상 경로 override (기본: /home/inseok/study/5g/inseokiki)
#
# 안전 규칙 (docs/PERSONAL_AI_ENV_MIGRATION_PLAN.md §10):
#   1. 기본은 dry-run — 변경 예정 내역만 출력.
#   2. 실제 복사는 --apply가 있을 때만 수행.
#   3. 기존 대상 파일을 무조건 덮어쓰지 않음 — 내용이 같으면 건너뜀.
#   4. 덮어쓰기 전 자동 백업(<파일>.bak.<타임스탬프>) 생성.
#   5. 삭제 동기화 없음 — 대상에만 있는 파일은 절대 건드리지 않음(스크립트에 삭제 로직 자체가 없음).
#   6. .git/, 빌드 산출물, 바이너리, 로그, 비밀정보 패턴은 복사 대상에서 제외.
#   7. 실행 시작 시 소스/대상 경로를 항상 출력.
#   8. 절대경로 검증 — 예상 밖의 위치로 실행되지 않도록 방어.
#   9. 실패 시 파일별 성공/실패를 명확히 보고.
#
# 심볼릭 링크를 만들지 않음 — 항상 실 파일 복사(cp -p).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$SCRIPT_DIR/.." && pwd)"   # .../phy_lab/personal
DST="/home/inseok/study/5g/inseokiki"
APPLY=0

for arg in "$@"; do
  case "$arg" in
    --apply) APPLY=1 ;;
    --dest=*) DST="${arg#--dest=}" ;;
    --help|-h)
      grep '^#' "$0" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "알 수 없는 옵션: $arg (--apply, --dest=<경로>만 지원)" >&2
      exit 1
      ;;
  esac
done

# --- 절대경로 검증 (요구사항 8) ---
if [[ "$SRC" != */personal ]]; then
  echo "오류: 이 스크립트는 personal/scripts/ 아래에서만 동작하도록 설계되었습니다. SRC=$SRC" >&2
  exit 1
fi
if [[ "$DST" != /* ]]; then
  echo "오류: --dest는 절대경로여야 합니다: $DST" >&2
  exit 1
fi
if [[ ! -d "$DST" ]]; then
  echo "오류: 대상 디렉터리가 존재하지 않습니다: $DST" >&2
  exit 1
fi
if [[ ! -d "$DST/.git" ]]; then
  echo "오류: 대상이 git 저장소가 아닙니다(.git 없음) — 잘못된 경로일 가능성: $DST" >&2
  exit 1
fi
DST="$(cd "$DST" && pwd)"
if [[ "$DST" == "$SRC"* || "$SRC" == "$DST"* ]]; then
  echo "오류: 소스와 대상 경로가 겹칩니다: SRC=$SRC DST=$DST" >&2
  exit 1
fi

# --- 파일 매핑 (docs/PERSONAL_AI_ENV_MIGRATION_PLAN.md §6) ---
# "src_rel:dst_rel" — src_rel이 디렉터리면 그 아래 전체를 재귀 매핑
MAPPINGS=(
  "AGENTS.md:AGENTS.md"
  "CLAUDE.md:CLAUDE.md"
  "tasks:tasks"
  "skills:.claude/skills"
  "agents:.claude/agents"
  "hooks:.claude/hooks"
  "scripts:scripts"
)

# 복사 허용 확장자(화이트리스트) / 제외 패턴(요구사항 6, 이중 방어)
ALLOWED_EXT_RE='\.(md|py|sh|json|txt)$'
EXCLUDE_RE='(^|/)\.git(/|$)|\.(o|a|so|log|bin|pem|p12|pfx)$|(^|/)\.env|id_rsa|id_ed25519|credentials\.json|\.netrc'

echo "=== sync_to_inseokiki.sh ==="
echo "SRC (기준본): $SRC"
echo "DST (적용본): $DST"
if [[ $APPLY -eq 1 ]]; then
  echo "모드: APPLY (실제 적용)"
else
  echo "모드: DRY-RUN"
fi
echo ""

# (src_abs, dst_rel) 쌍 목록을 만든다
declare -a PAIRS=()
for m in "${MAPPINGS[@]}"; do
  src_rel="${m%%:*}"
  dst_rel="${m#*:}"
  src_abs="$SRC/$src_rel"
  if [[ -f "$src_abs" ]]; then
    PAIRS+=("$src_abs|$dst_rel")
  elif [[ -d "$src_abs" ]]; then
    while IFS= read -r -d '' f; do
      rel="${f#"$src_abs"/}"
      full_rel_for_filter="$src_rel/$rel"
      if [[ "$full_rel_for_filter" =~ $EXCLUDE_RE ]]; then
        continue
      fi
      if [[ ! "$f" =~ $ALLOWED_EXT_RE ]]; then
        echo "건너뜀(비허용 확장자): $src_rel/$rel" >&2
        continue
      fi
      PAIRS+=("$f|$dst_rel/$rel")
    done < <(find "$src_abs" -type f -print0 | sort -z)
  else
    echo "경고: 소스 경로가 없습니다(건너뜀): $src_abs" >&2
  fi
done

NEW=0 IDENTICAL=0 MODIFIED=0 FAILED=0
printf "%-10s %-40s -> %s\n" "STATUS" "SRC" "DST"
printf "%-10s %-40s -> %s\n" "------" "---" "---"

for pair in "${PAIRS[@]}"; do
  src_abs="${pair%%|*}"
  dst_rel="${pair#*|}"
  dst_abs="$DST/$dst_rel"
  src_rel="${src_abs#"$SRC"/}"

  if [[ ! -e "$dst_abs" ]]; then
    status="NEW"
    NEW=$((NEW+1))
  elif cmp -s "$src_abs" "$dst_abs"; then
    status="IDENTICAL"
    IDENTICAL=$((IDENTICAL+1))
  else
    status="MODIFIED"
    MODIFIED=$((MODIFIED+1))
  fi
  printf "%-10s %-40s -> %s\n" "$status" "$src_rel" "$dst_rel"

  if [[ $APPLY -eq 1 && "$status" != "IDENTICAL" ]]; then
    dst_dir="$(dirname "$dst_abs")"
    if ! mkdir -p "$dst_dir" 2>/tmp/sync_err.$$; then
      echo "  실패(디렉터리 생성): $dst_rel — $(cat /tmp/sync_err.$$)" >&2
      rm -f /tmp/sync_err.$$
      FAILED=$((FAILED+1))
      continue
    fi
    rm -f /tmp/sync_err.$$
    if [[ "$status" == "MODIFIED" ]]; then
      backup="${dst_abs}.bak.$(date +%Y%m%d%H%M%S)"
      if ! cp -p "$dst_abs" "$backup"; then
        echo "  실패(백업 생성): $dst_rel" >&2
        FAILED=$((FAILED+1))
        continue
      fi
      echo "  백업: $backup"
    fi
    if cp -p "$src_abs" "$dst_abs"; then
      echo "  적용됨: $dst_rel"
    else
      echo "  실패(복사): $dst_rel" >&2
      FAILED=$((FAILED+1))
    fi
  fi
done

echo ""
echo "=== 요약 ==="
echo "NEW=$NEW  MODIFIED=$MODIFIED  IDENTICAL=$IDENTICAL  FAILED=$FAILED"
if [[ $APPLY -eq 0 ]]; then
  echo "(dry-run — 실제로는 아무 파일도 쓰지 않았습니다. 적용하려면 --apply)"
elif [[ $FAILED -gt 0 ]]; then
  echo "일부 파일 적용 실패 — 위 실패 목록을 확인하세요. 성공한 파일은 이미 적용된 상태입니다."
  exit 1
else
  echo "적용 완료."
fi
