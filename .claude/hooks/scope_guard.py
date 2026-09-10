#!/usr/bin/env python3
# scope_guard.py — 프로젝트별 "수정 범위" 규칙의 결정론적 강제
# CLAUDE_PROJECT_DIR/.claude/scope_allowed_dirs.txt 가 있으면 그 안에 나열된
# 경로 prefix 밖의 편집을 차단한다. 설정 파일이 없으면 이 훅은 아무 제한도
# 걸지 않는다(기본 비활성 — 프로젝트가 원할 때만 opt-in).
# (exit 2 = 차단 + stderr를 Claude에게 전달)
#
# 실패 정책(2026-09-10, H-03): "설정 파일이 없음"(이 프로젝트가 scope 제한을
# 아예 쓰지 않기로 한 정상 상태)과 "입력을 못 읽음 / 설정을 읽다가 실패함 /
# 설정이 있는데 비어 있음"(이 훅 자체나 설정이 오작동/오구성인 비정상 상태)을
# 구분한다. 전자는 통과(exit 0)가 맞고, 후자는 차단(fail-closed, exit 2)한다 —
# scope 제한이 실제로 켜져 있는 프로젝트에서 이 훅이 고장났다고 조용히 무제한
# 허용으로 빠지면 "결정론적 강제"라는 설계 의도와 맞지 않는다.
import json
import os
import sys


def _block(reason):
    sys.stderr.write(
        "[scope_guard] 입력/설정을 처리할 수 없어 안전하게 차단했습니다: " + reason + "\n"
        "이 훅은 실패 시 차단(fail-closed) 정책입니다. 반복되면 hook 입력 형식이나 "
        "scope_allowed_dirs.txt 상태를 확인하세요.\n"
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

tool_input = data.get("tool_input", {})
if not isinstance(tool_input, dict):
    tool_input = {}
path = tool_input.get("file_path", "") or tool_input.get("path", "")
if not isinstance(path, str) or not path:
    sys.exit(0)  # 이 도구 호출은 파일 경로가 없음 — scope_guard의 관할 아님

event_cwd = data.get("cwd", "")
if not isinstance(event_cwd, str):
    event_cwd = ""
proj = os.environ.get("CLAUDE_PROJECT_DIR", "") or event_cwd
if not proj:
    sys.exit(0)  # 프로젝트 루트를 알 수 없으면(실전에서는 도달하지 않는 경로) 판단 보류

config_path = os.path.join(proj, ".claude", "scope_allowed_dirs.txt")
if not os.path.exists(config_path):
    sys.exit(0)  # 이 프로젝트는 scope 제한을 쓰지 않음 — 정상적인 opt-out

try:
    with open(config_path, encoding="utf-8") as f:
        allowed_dirs = [
            line.strip() for line in f
            if line.strip() and not line.strip().startswith("#")
        ]
except OSError as e:
    _block(f"scope_allowed_dirs.txt 읽기 실패: {e}")

if not allowed_dirs:
    _block("scope_allowed_dirs.txt가 존재하지만 유효한 항목이 하나도 없습니다 — "
           "의도가 불분명해 안전하게 차단합니다(빈 설정으로 모든 편집을 허용하는 "
           "이전 동작은 오구성 시 보호가 조용히 사라지는 문제가 있었습니다).")

# path가 상대경로면 이 훅 프로세스 자신의 cwd가 아니라, 도구 호출 시점의 cwd
# (event의 cwd, 없으면 CLAUDE_PROJECT_DIR)를 기준으로 절대화한다 — 이전에는
# os.path.abspath(path)를 그대로 써서 훅 프로세스의 cwd가 다르면 잘못된
# 경로로 판정될 수 있었다.
base_for_relative = event_cwd or proj
abs_path = path if os.path.isabs(path) else os.path.normpath(os.path.join(base_for_relative, path))

proj_real = os.path.realpath(proj)
abs_real = os.path.realpath(abs_path)

# 프로젝트 밖 파일(/tmp, 홈 등 스크래치)은 이 훅의 관할이 아님 — realpath 기준으로
# 판정해 symlink로 프로젝트 안처럼 보이게 우회하는 것도 막는다.
try:
    within_project = os.path.commonpath([proj_real, abs_real]) == proj_real
except ValueError:
    within_project = False
if not within_project:
    sys.exit(0)

rel = "/" + os.path.relpath(abs_real, proj_real).replace("\\", "/")

# 항상 허용하는 파일명(위치 무관) — 운영 문서류
ALLOWED_BASENAMES = ["CLAUDE.md", "AGENTS.md", "README.md", ".gitignore"]

if os.path.basename(rel) in ALLOWED_BASENAMES:
    sys.exit(0)

# 허용 판정은 경로 구성요소 단위 prefix 비교로 한다 — 예전엔 `allowed in probe`
# 부분 문자열 검사라 allowed_dirs에 "src"가 있으면 "src_backup/evil.py"도
# 허용되는 오탐(이름만 비슷한 디렉토리)이 있었다.
rel_parts = [p for p in rel.split("/") if p]
allowed = False
for a in allowed_dirs:
    a_parts = [p for p in a.strip("/").split("/") if p]
    if a_parts and rel_parts[:len(a_parts)] == a_parts:
        allowed = True
        break

if allowed:
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
