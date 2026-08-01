#!/usr/bin/env python3
# scope_guard.py — 프로젝트별 "수정 범위" 규칙의 결정론적 강제
# CLAUDE_PROJECT_DIR/.claude/scope_allowed_dirs.txt 가 있으면 그 안에 나열된
# 경로 prefix 밖의 편집을 차단한다. 설정 파일이 없으면 이 훅은 아무 제한도
# 걸지 않는다(기본 비활성 — 프로젝트가 원할 때만 opt-in).
# (exit 2 = 차단 + stderr를 Claude에게 전달)
import json
import os
import sys

try:
    data = json.load(sys.stdin)
except Exception:
    sys.exit(0)

tool_input = data.get("tool_input", {})
path = tool_input.get("file_path", "") or tool_input.get("path", "")
if not path:
    sys.exit(0)

proj = os.environ.get("CLAUDE_PROJECT_DIR", "") or data.get("cwd", "")
if not proj:
    sys.exit(0)

config_path = os.path.join(proj, ".claude", "scope_allowed_dirs.txt")
if not os.path.exists(config_path):
    sys.exit(0)  # 설정 파일이 없으면 이 프로젝트는 scope 제한을 쓰지 않음

try:
    with open(config_path, encoding="utf-8") as f:
        allowed_dirs = [
            line.strip() for line in f
            if line.strip() and not line.strip().startswith("#")
        ]
except OSError:
    sys.exit(0)

if not allowed_dirs:
    sys.exit(0)

abs_path = os.path.abspath(path)

# 프로젝트 밖 파일(/tmp, 홈 등 스크래치)은 이 훅의 관할이 아님
if not abs_path.startswith(os.path.abspath(proj) + os.sep):
    sys.exit(0)

rel = "/" + os.path.relpath(abs_path, proj).replace("\\", "/")

# 항상 허용하는 파일명(위치 무관) — 운영 문서류
ALLOWED_BASENAMES = ["CLAUDE.md", "AGENTS.md", "README.md", ".gitignore"]

probe = rel if rel.endswith("/") else rel + "/"
if any(a in probe for a in allowed_dirs) or os.path.basename(rel) in ALLOWED_BASENAMES:
    sys.exit(0)

sys.stderr.write(
    "[scope_guard] 담당 범위 밖 파일 수정 시도가 차단되었습니다: " + rel + "\n"
    "이 프로젝트의 .claude/scope_allowed_dirs.txt에 정의된 범위 밖입니다: "
    + ", ".join(allowed_dirs) + " 및 CLAUDE.md/AGENTS.md/README.md/.gitignore(위치 무관)만 허용됩니다.\n"
    "지금 할 일: (1) 이 파일을 수정하지 말고, (2) 왜 이 파일 수정이 필요하다고 판단했는지 "
    "원인 분석과 제안 변경 내용을 사용자에게 보고하고, (3) 사용자의 명시적 승인을 받은 경우에만 "
    "다시 시도하세요. 승인 후에도 변경은 최소 범위로 제한하세요.\n"
)
sys.exit(2)
