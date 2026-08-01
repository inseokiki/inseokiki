#!/usr/bin/env python3
# secret_guard.py — 자동 승인(bypassPermissions) 환경에서 민감 파일 읽기 차단
# 권한 모드와 무관하게 결정론적으로 동작하는 방어선. (settings.json의 deny 규칙과 이중 방어)
import json
import sys

SENSITIVE = [
    "/.env", ".env.local", ".env.production",
    "/.ssh/", "id_rsa", "id_ed25519",
    "/.aws/", "credentials.json", ".netrc", ".npmrc",
    ".pem", ".p12", ".pfx",
]

try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)

ti = data.get("tool_input", {})
path = (ti.get("file_path", "") or ti.get("path", "") or ti.get("pattern", "")).lower()
if not path:
    sys.exit(0)

hit = [s for s in SENSITIVE if s in path]
if not hit:
    sys.exit(0)

sys.stderr.write(
    "[secret_guard] 민감 파일 접근이 차단되었습니다: " + path + "\n"
    "이 경로는 자격증명/키 파일 패턴에 해당합니다. 내용을 읽지 말고, "
    "해당 파일이 작업에 꼭 필요하다면 사용자에게 이유를 설명하고 직접 확인을 요청하세요.\n"
)
sys.exit(2)
