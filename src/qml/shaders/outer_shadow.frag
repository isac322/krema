#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;

    float panelWidth;
    float panelHeight;
    float cornerRadius;
    float elevation;
    float lightX;
    float lightY;
    float lightZ;
    float lightRadius;
    float shadowR;
    float shadowG;
    float shadowB;
    float shadowA;
    float margin;
};

float roundedBoxSDF(vec2 center, vec2 halfSize, float radius) {
    vec2 d = abs(center) - halfSize + vec2(radius);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;
}

// erf approximation (Abramowitz & Stegun, max error ~5e-4)
float erf_approx(float x) {
    float s = sign(x);
    float a = abs(x);
    float t = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    t *= t;
    return s - s / (t * t);
}

void main() {
    // Total ShaderEffect size = panel + margin on each side
    vec2 totalSize = vec2(panelWidth + margin * 2.0,
                          panelHeight + margin * 2.0);

    // Convert tex coords to ground-plane coordinates (panel center = origin)
    vec2 groundPos = qt_TexCoord0 * totalSize - totalSize * 0.5;

    // Project: ray from light L through ground pixel P hits panel plane (z = elevation)
    // t = (Lz - elevation) / Lz
    float t = (lightZ - elevation) / lightZ;
    vec2 lightXY = vec2(lightX, lightY);
    vec2 Q = lightXY + t * (groundPos - lightXY);

    // Evaluate panel SDF at projected point
    vec2 halfSize = vec2(panelWidth, panelHeight) * 0.5;
    float d = roundedBoxSDF(Q, halfSize, cornerRadius);

    // lightRadius = Gaussian sigma directly (bigger light = softer shadow)
    float sigma = max(lightRadius, 0.5);

    // erf-based Gaussian-convolved SDF shadow
    // erf(d / (sigma * sqrt(2))) gives exact Gaussian blur for straight edges
    float shadow = 0.5 - 0.5 * erf_approx(d / (sigma * 1.4142135));

    // Mask out shadow inside the panel body (prevents dark overlay on panel)
    float panelDist = roundedBoxSDF(groundPos, halfSize, cornerRadius);
    shadow *= smoothstep(-1.5, 0.5, panelDist);

    float alpha = shadow * shadowA * qt_Opacity;
    fragColor = vec4(shadowR * alpha, shadowG * alpha, shadowB * alpha, alpha);
}
