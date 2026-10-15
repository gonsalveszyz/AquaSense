#!/usr/bin/env bash
set -euo pipefail
EXE="${1:-./build/aquasense}"
RESULT="$(printf '2\n3\n4\n0\n' | "$EXE" --demo)"
printf '%s\n' "$RESULT" | grep -q 'GRAFICO DE BARRAS - Temperatura'
printf '%s\n' "$RESULT" | grep -q 'GRAFICO DE BARRAS - pH'
printf '%s\n' "$RESULT" | grep -q 'GRAFICO DE BARRAS - Turbidez'
printf '%s\n' "$RESULT" | grep -q 'ESTATISTICAS DA AMOSTRA'
printf '%s\n' "$RESULT" | grep -q 'AquaSense encerrado.'
echo 'PASSOU: menus, tabela, 3 graficos e estatisticas no modo demo.'
