#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;

    // Tint color (from DockView.backgroundColor)
    float tintR;
    float tintG;
    float tintB;
    float tintOpacity;

    // Noise
    float noiseStrength;
    float resX;
    float resY;

    // Rounded corner mask
    float cornerRadius;
};

// Hash-based noise (deterministic, no animation)
float hash21(vec2 p) {
    p = fract(p * vec2(233.34, 851.73));
    p += dot(p, p + 23.45);
    return fract(p.x * p.y);
}

// SDF for a rounded rectangle centered at the origin
float roundedBoxSDF(vec2 center, vec2 halfSize, float radius) {
    vec2 d = abs(center) - halfSize + vec2(radius);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - radius;
}

void main() {
    // Layer 1: Tint overlay
    vec3 result = vec3(tintR, tintG, tintB);

    // Layer 2: Per-pixel noise grain
    if (noiseStrength > 0.001) {
        vec2 noiseCoord = floor(qt_TexCoord0 * vec2(resX, resY));
        float n = hash21(noiseCoord);
        result += (n - 0.5) * noiseStrength;
    }

    // Rounded corner mask via SDF
    vec2 pixelPos = qt_TexCoord0 * vec2(resX, resY);
    vec2 center = pixelPos - vec2(resX, resY) * 0.5;
    vec2 halfSize = vec2(resX, resY) * 0.5;
    float dist = roundedBoxSDF(center, halfSize, cornerRadius);
    float mask = 1.0 - smoothstep(-0.5, 0.5, dist);

    // Premultiplied alpha output (Qt Quick uses GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
    float alpha = tintOpacity * mask * qt_Opacity;
    fragColor = vec4(clamp(result, 0.0, 1.0) * alpha, alpha);
}
