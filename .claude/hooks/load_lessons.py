#!/usr/bin/env python3
# load_lessons.py — SessionStart 시 tasks/lessons.md를 컨텍스트로 자동 주입
# "세션 시작 시 교훈 목록 먼저 검토" 규칙의 결정론적 강제.
# SessionStart 훅의 stdout은 Claude의 컨텍스트에 추가된다.
import os
import sys

proj = os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
lessons = os.path.join(proj, "tasks", "lessons.md")

if not os.path.exists(lessons):
    sys.exit(0)

try:
    with open(lessons, encoding="utf-8") as f:
        content = f.read().strip()
except OSError:
    sys.exit(0)

if not content:
    sys.exit(0)

# 과도한 컨텍스트 주입 방지: 8000자 초과 시 최근 부분만
LIMIT = 8000
if len(content) > LIMIT:
    content = "(...앞부분 생략 — 전체는 tasks/lessons.md 참조...)\n" + content[-LIMIT:]

print("[자동 주입: tasks/lessons.md — 이 프로젝트에서 과거 실수로부터 기록된 교훈. 작업 전 반드시 반영할 것]")
print()
print(content)
sys.exit(0)
