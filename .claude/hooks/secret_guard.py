#!/usr/bin/env python3
# secret_guard.py — 자동 승인(bypassPermissions) 환경에서 민감 파일 읽기 차단
# 권한 모드와 무관하게 결정론적으로 동작하는 방어선. (settings.json의 deny 규칙과 이중 방어)
#
# 실패 정책(2026-09-10, lab/HARNESS_ANALYSIS.md H-03): 입력을 파싱하지 못하면
# "안전하지 않다"고 보고 차단한다(fail-closed) — 이 훅이 존재하는 이유 자체가
# "무슨 일이 있어도 민감 파일 접근을 놓치지 않기" 위해서이므로, 예외적인 입력
# 형식 변경/오류 상황에서 조용히 통과시키는 이전 동작(fail-open,
# `except Exception: sys.exit(0)`)은 이 훅의 존재 목적과 모순된다.
import json
import sys

SENSITIVE = [
    "/.env", ".env.local", ".env.production",
    "/.ssh/", "id_rsa", "id_ed25519",
    "/.aws/", "credentials.json", ".netrc", ".npmrc",
    ".pem", ".p12", ".pfx",
]


def _block(reason):
    sys.stderr.write(
        "[secret_guard] 입력을 해석할 수 없어 안전하게 차단했습니다: " + reason + "\n"
        "이 훅은 실패 시 차단(fail-closed) 정책입니다 — 정상적인 도구 호출이라면 이 오류가 "
        "반복되지 않아야 합니다. 반복되면 hook 설정/입력 형식 변경 여부를 확인하세요.\n"
    )
    sys.exit(2)


try:
    raw = sys.stdin.read()
except OSError as e:
    _block(f"stdin 읽기 실패: {e}")

try:
    data = json.loads(raw)
except (json.JSONDecodeError, ValueError) as e:
    _block(f"JSON 파싱 실패: {e}")

if not isinstance(data, dict):
    _block(f"입력이 JSON 객체가 아닙니다: {type(data).__name__}")

ti = data.get("tool_input", {})
if not isinstance(ti, dict):
    ti = {}
path = ti.get("file_path", "") or ti.get("path", "") or ti.get("pattern", "")
if not isinstance(path, str):
    path = str(path)
path = path.lower()
if not path:
    sys.exit(0)

# 최상위 파일(".env")과 그 상대경로 표기("./.env")를 하위 경로("project/.env")와
# 동일하게 차단하도록 선행 "/"를 붙여 정규화한다 — backup_sync.py의 is_sensitive()
# 에 2026-08-31 반영된 것과 같은 수정을 이 훅에도 적용(H-03/H-10, 2026-09-10).
# "/" + "./.env" = "/./.env"이고 이 문자열은 "/.env"를 부분 문자열로 포함하므로
# 별도의 "./" 제거 없이도 매치된다.
normalized = "/" + path
hit = [s for s in SENSITIVE if s in normalized]
if not hit:
    sys.exit(0)

sys.stderr.write(
    "[secret_guard] 민감 파일 접근이 차단되었습니다: " + path + "\n"
    "이 경로는 자격증명/키 파일 패턴에 해당합니다. 내용을 읽지 말고, "
    "해당 파일이 작업에 꼭 필요하다면 사용자에게 이유를 설명하고 직접 확인을 요청하세요.\n"
)
sys.exit(2)
