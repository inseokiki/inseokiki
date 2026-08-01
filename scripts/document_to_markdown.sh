#!/usr/bin/env bash
# document_to_markdown.sh — DOCX/PDF 원본을 AI 검색용 Markdown/텍스트로 변환
#
# 사용법:
#   document_to_markdown.sh <입력파일> [출력파일]
#
# 출력파일을 생략하면 입력파일과 같은 디렉터리에 확장자만 바꿔 생성한다.
# 원본은 절대 수정/삭제하지 않는다.
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "사용법: $0 <입력파일.docx|입력파일.pdf> [출력파일]" >&2
  exit 1
fi

IN="$1"
if [[ ! -f "$IN" ]]; then
  echo "오류: 입력 파일을 찾을 수 없습니다: $IN" >&2
  exit 1
fi

EXT="${IN##*.}"
EXT_LOWER=$(echo "$EXT" | tr '[:upper:]' '[:lower:]')
BASE="${IN%.*}"

case "$EXT_LOWER" in
  docx)
    OUT="${2:-${BASE}.md}"
    if ! command -v pandoc >/dev/null 2>&1; then
      echo "오류: pandoc이 설치되어 있지 않습니다." >&2
      exit 1
    fi
    pandoc "$IN" -t gfm -o "$OUT"
    echo "변환 완료: $IN -> $OUT"
    ;;
  pdf)
    OUT="${2:-${BASE}.txt}"
    if ! command -v pdftotext >/dev/null 2>&1; then
      echo "오류: pdftotext(poppler-utils)가 설치되어 있지 않습니다." >&2
      exit 1
    fi
    pdftotext "$IN" "$OUT"
    SIZE=$(wc -c < "$OUT" | tr -d ' ')
    if [[ "$SIZE" -lt 200 ]]; then
      echo "경고: 변환 결과가 매우 짧습니다(${SIZE} bytes) — 스캔 PDF(이미지 기반)일 가능성이 높습니다. OCR(예: ocrmypdf)이 별도로 필요합니다." >&2
    fi
    echo "변환 완료: $IN -> $OUT"
    ;;
  *)
    echo "오류: 지원하지 않는 확장자입니다(.docx 또는 .pdf만 지원): $IN" >&2
    exit 1
    ;;
esac
