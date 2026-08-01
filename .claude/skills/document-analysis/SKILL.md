---
name: document-analysis
description: 3GPP DOCX/PDF 등 원본 문서를 AI가 검색·이해하기 쉬운 Markdown/텍스트로 변환해야 할 때 사용. 새 스펙 문서를 받았거나, 기존 문서가 아직 변환되지 않아 grep/검색이 안 될 때 트리거.
---

# 문서 → Markdown/텍스트 변환 표준 절차

## 0. 원칙
- 원본 파일(DOCX/PDF)은 항상 보존한다 — 변환본을 별도 파일로 생성하고, 원본을 덮어쓰거나 삭제하지 않는다.
- 기존 `3gpp/` 디렉터리 구조를 임의로 재배치하지 않는다. 경로 변경이 필요하면 먼저 참조 여부를 조사하고 사용자 승인을 받는다.

## 1. 변환 방법

```bash
# DOCX → GitHub Flavored Markdown
pandoc input.docx -t gfm -o output.md

# 텍스트 기반 PDF → text
pdftotext input.pdf output.txt
```

동일 기능을 `scripts/document_to_markdown.sh <입력파일> [출력경로]`로 실행할 수 있다.

## 2. 스캔 PDF 주의
텍스트 레이어가 없는 스캔 PDF는 `pdftotext`로 변환해도 결과가 거의 비어 있거나 의미 없는 문자가 나온다. 이 경우 별도 OCR(예: `ocrmypdf`, Tesseract)이 필요하다는 사실을 사용자에게 알리고, 자동으로 OCR을 시도하지 않는다.

## 3. 권장 디렉터리 구조

```text
3gpp/
├── original/     # 원본 DOCX/PDF (보존)
├── markdown/     # AI 검색용 변환본
└── index.md      # 문서 번호, Release, 버전, 변환 상태
```

`index.md`에는 문서 번호(예: TS 38.211), Release, 버전, 변환 여부/날짜를 표로 관리한다.

## 4. 변환 후
- 변환본이 생성되면 `index.md`를 갱신한다.
- `spec-lookup` 에이전트가 `markdown/`을 우선 검색하므로, 변환이 끝난 문서는 이후 조회가 빨라진다.
