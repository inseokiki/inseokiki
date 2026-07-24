# Dotfiles 세팅 가이드 (vim + VS Code Test Theme)

개인 저장소(`inseokiki/inseokiki`)에 vim/VS Code 테마 설정을 정리해서, 새 환경(맥북, WSL 재설치 등)에서도 명령어 몇 줄로 그대로 재현할 수 있게 만드는 가이드입니다.

---

## 1. 폴더 구조 만들고 기존 파일 복사

```bash
mkdir -p ~/develop/KANG/inseokiki/dotfiles/test-theme/themes

cp ~/.vimrc ~/develop/KANG/inseokiki/dotfiles/vimrc
cp /mnt/c/Users/kang0/.vscode/extensions/test-theme-0.0.1/package.json \
   ~/develop/KANG/inseokiki/dotfiles/test-theme/
cp /mnt/c/Users/kang0/.vscode/extensions/test-theme-0.0.1/themes/test.json \
   ~/develop/KANG/inseokiki/dotfiles/test-theme/themes/
```

**최종 구조:**
```
inseokiki/
└── dotfiles/
    ├── vimrc
    ├── install.sh
    ├── register_theme.py
    └── test-theme/
        ├── package.json
        └── themes/
            └── test.json
```

---

## 2. install.sh 생성 (새 환경 세팅용)

```bash
cat > ~/develop/KANG/inseokiki/dotfiles/install.sh << 'EOF'
#!/bin/bash
set -e

DOTFILES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# vimrc 적용
cp "$DOTFILES_DIR/vimrc" ~/.vimrc
echo "vimrc 적용 완료"

# VS Code 테마 적용 (Windows .vscode 경로를 인자로 받음)
if [ -n "$1" ]; then
    VSCODE_EXT_DIR="$1/.vscode/extensions/test-theme-0.0.1"
    mkdir -p "$VSCODE_EXT_DIR/themes"
    cp "$DOTFILES_DIR/test-theme/package.json" "$VSCODE_EXT_DIR/"
    cp "$DOTFILES_DIR/test-theme/themes/test.json" "$VSCODE_EXT_DIR/themes/"
    echo "VS Code 테마 파일 복사 완료: $VSCODE_EXT_DIR"
    echo "-> extensions.json 등록은 별도로 진행 필요 (register_theme.py 참고)"
else
    echo "VS Code 테마는 스킵 (경로 인자 없음). 예: ./install.sh /mnt/c/Users/kang0"
fi
EOF
chmod +x ~/develop/KANG/inseokiki/dotfiles/install.sh
```

---

## 3. register_theme.py 생성 (extensions.json 자동 등록)

```bash
cat > ~/develop/KANG/inseokiki/dotfiles/register_theme.py << 'EOF'
"""
VS Code extensions.json에 test-theme 수동 등록
사용법: python3 register_theme.py /mnt/c/Users/kang0
"""
import json
import sys
import time

if len(sys.argv) < 2:
    print("사용법: python3 register_theme.py <windows_home_경로>")
    print("예: python3 register_theme.py /mnt/c/Users/kang0")
    sys.exit(1)

win_home = sys.argv[1]
path = f"{win_home}/.vscode/extensions/extensions.json"

with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

# 이미 등록돼 있으면 중복 추가 방지
if any(e.get("identifier", {}).get("id") == "test-theme" for e in data):
    print("이미 등록되어 있습니다.")
    sys.exit(0)

data.append({
    "identifier": {"id": "test-theme"},
    "version": "0.0.1",
    "location": {
        "$mid": 1,
        "path": "/c:/Users/kang0/.vscode/extensions/test-theme-0.0.1",
        "scheme": "file"
    },
    "relativeLocation": "test-theme-0.0.1",
    "metadata": {
        "installedTimestamp": int(time.time() * 1000),
        "pinned": False,
        "source": "gallery"
    }
})

with open(path, "w", encoding="utf-8") as f:
    json.dump(data, f)

print("등록 완료! VS Code 완전 종료 후(taskkill /F /IM Code.exe) 재시작하세요.")
EOF
```

---

## 4. 확인

```bash
find ~/develop/KANG/inseokiki/dotfiles -type f
```

아래처럼 나오면 정상:
```
vimrc
install.sh
register_theme.py
test-theme/package.json
test-theme/themes/test.json
```

---

## 5. git commit & push

```bash
cd ~/develop/KANG/inseokiki
git add dotfiles/
git commit -m "add vim/vscode theme dotfiles + install script"
git push
```

---

## 새 환경에서 나중에 쓸 때 (맥북, 재설치한 WSL 등)

```bash
git clone git@github.com:inseokiki/inseokiki.git
cd inseokiki/dotfiles

# vimrc + 테마 파일 복사
./install.sh /mnt/c/Users/kang0

# extensions.json 등록
python3 register_theme.py /mnt/c/Users/kang0
```

이후 VS Code 완전 종료(`taskkill /F /IM Code.exe`) → 재시작 → `Ctrl+Shift+P` → "색 테마 선택" → **Test Theme**

---

## 참고 — 트러블슈팅

| 증상 | 원인 | 해결 |
|---|---|---|
| 테마가 목록에 안 보임 | extensions.json 미등록 | `register_theme.py` 실행 |
| VS Code 재시작해도 그대로 | 창만 닫고 프로세스는 살아있음 | `taskkill /F /IM Code.exe`로 완전 종료 후 재시작 |
| Remote-SSH 세션에서 테마 안 보임 | 로컬(Windows)에만 설치했는데 SSH 서버 쪽 `.vscode-server`에 남아있는 이전 설정과 충돌 | 위 방식(로컬 `.vscode/extensions`)만 쓰면 SSH 세션에서도 자동 적용됨 — 별도 조치 불필요 |
