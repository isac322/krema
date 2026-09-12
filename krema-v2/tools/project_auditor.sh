#!/bin/bash
echo "=== KREMA MASTER PROJECT AUDITOR ==="
echo "Searching for Scale-Dependent Logic..."

echo -e "\n1. Orbit & Hitbox Thresholds:"
grep -rnE "Orbit|Threshold|Hitbox|isInside" src/qml | grep -v "Binary file"

echo -e "\n2. Zoom Factor Usages:"
grep -rn "maxZoomFactor" src | grep -v "Binary file"

echo -e "\n3. Signal Connections (C++):"
grep -rn "connect(" src/shell src/app | grep -E "Settings|Changed"

echo -e "\n4. Coordinate Mapping (Cross-Axis):"
grep -rnE "parent.width|parent.height|secondaryAxis" src/qml

echo -e "\nAudit Complete."
