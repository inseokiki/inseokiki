#!/bin/bash

# Copy MATLAB scripts to Windows MATLAB folder
# Run this after editing any .m file

DST="/mnt/c/Users/kang0/OneDrive/문서/MATLAB"

mkdir -p "$DST"

cp plot_bler.m          "$DST/"
cp plot_ber.m           "$DST/"
cp plot_constellation.m "$DST/"
cp result.txt           "$DST/" 2>/dev/null
cp ber_result.txt       "$DST/" 2>/dev/null
cp iq_dump.txt          "$DST/" 2>/dev/null

# Generate a reload helper so MATLAB picks up the latest versions
cat > "$DST/reload_ber.m" << 'EOF'
% Run this in MATLAB instead of plot_ber to force reload after copy.sh
clear plot_ber
clear functions
plot_ber
EOF

echo "Copied -> $DST"
echo "MATLAB에서: reload_ber"
