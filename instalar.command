#!/bin/bash
# ---------------------------------------------------------------------------
#  Instala SpectroCheck (ya compilado por GitHub) en tu Mac.
#  Uso en la Terminal:  bash ~/Downloads/SpectroCheck-mac/instalar.command
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"

AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU_DIR" "$VST3_DIR"

if [ ! -d "SpectroCheck.component" ]; then
    echo "❌ No encuentro 'SpectroCheck.component' en esta carpeta."
    echo "   Asegurate de haber descomprimido SpectroCheck-mac.zip."
    exit 1
fi

echo "📦 Copiando los plugins..."
rm -rf "$AU_DIR/SpectroCheck.component" "$VST3_DIR/SpectroCheck.vst3"
cp -R "SpectroCheck.component" "$AU_DIR/"
[ -d "SpectroCheck.vst3" ] && cp -R "SpectroCheck.vst3" "$VST3_DIR/"

# Los archivos bajados de internet quedan "en cuarentena": esto se la saca.
xattr -dr com.apple.quarantine "$AU_DIR/SpectroCheck.component" 2>/dev/null || true
xattr -dr com.apple.quarantine "$VST3_DIR/SpectroCheck.vst3" 2>/dev/null || true

# Avisarle a macOS que hay un Audio Unit nuevo.
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
sleep 1

echo "🔍 Validando (lo mismo que hace Logic)..."
if auval -v aufx Spck Ms06 2>&1 | grep -q "AU VALIDATION SUCCEEDED"; then
    echo "✅ Listo. Abrí Logic y buscalo en Audio Units > 0600musica > SpectroCheck."
else
    echo "⚠️  Quedó instalado, pero la validación no dio OK todavía."
    echo "   Abrí Logic igual; si no aparece, mandame lo que dice:  auval -v aufx Spck Ms06"
fi
