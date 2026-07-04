#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 fragTint;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform ObjectPushConstants {
    mat4 model;
    mat4 previousModel;
    vec4 tint;
    vec4 materialBaseColorFactor;
    vec4 materialControls;
    vec4 materialCustom;
    vec4 viewport;
    vec4 cameraControls;
    vec4 cameraPosition;
    vec4 cameraDirection;
} objectData;

layout(set = 1, binding = 0) uniform sampler2D fallbackTexture;
layout(set = 1, binding = 1) uniform sampler2D colorMap;
layout(set = 1, binding = 2) uniform samplerCube galaxy;

const float PI = 3.14159265359;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

float smootherstep(float edge0, float edge1, float value) {
    float x = saturate((value - edge0) / max(edge1 - edge0, 0.0001));
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

float snoise(vec3 v);

float fbm(vec3 p) {
    float value = 0.0;
    float amplitude = 0.5;

    for (int i = 0; i < 5; ++i) {
        value += amplitude * (0.5 * snoise(p) + 0.5);
        p = p * 2.03 + vec3(7.1, 3.5, 1.7);
        amplitude *= 0.5;
    }

    return value;
}

vec3 accel(float h2, vec3 pos) {
    float r2 = dot(pos, pos);
    float r5 = r2 * r2 * sqrt(max(r2, 0.0001));
    return -1.5 * h2 * pos / max(r5, 0.0001);
}

mat3 lookAt(vec3 origin, vec3 target, float roll) {
    vec3 rr = vec3(sin(roll), cos(roll), 0.0);
    vec3 ww = normalize(target - origin);
    vec3 uu = normalize(cross(ww, rr));
    vec3 vv = normalize(cross(uu, ww));

    return mat3(uu, vv, ww);
}

vec4 quadFromAxisAngle(vec3 axis, float angle) {
    float halfAngle = angle * 0.5;
    return vec4(axis * sin(halfAngle), cos(halfAngle));
}

vec4 quadConj(vec4 q) {
    return vec4(-q.x, -q.y, -q.z, q.w);
}

vec4 quatMult(vec4 q1, vec4 q2) {
    return vec4(
        (q1.w * q2.x) + (q1.x * q2.w) + (q1.y * q2.z) - (q1.z * q2.y),
        (q1.w * q2.y) - (q1.x * q2.z) + (q1.y * q2.w) + (q1.z * q2.x),
        (q1.w * q2.z) + (q1.x * q2.y) - (q1.y * q2.x) + (q1.z * q2.w),
        (q1.w * q2.w) - (q1.x * q2.x) - (q1.y * q2.y) - (q1.z * q2.z)
    );
}

vec3 rotateVector(vec3 position, vec3 axis, float angle) {
    vec4 q = quadFromAxisAngle(normalize(axis), angle);
    vec4 qConj = quadConj(q);
    vec4 p = vec4(position, 0.0);
    vec4 rotated = quatMult(quatMult(q, p), qConj);
    return rotated.xyz;
}

vec4 permute(vec4 x) {
    return mod(((x * 34.0) + 1.0) * x, 289.0);
}

vec4 taylorInvSqrt(vec4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

float snoise(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod(i, 289.0);
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0)) +
        i.y + vec4(0.0, i1.y, i2.y, 1.0)) +
        i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 1.0 / 7.0;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);
    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(
        dot(p0, p0),
        dot(p1, p1),
        dot(p2, p2),
        dot(p3, p3)
    ));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    vec4 m = max(
        0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)),
        0.0
    );
    m = m * m;
    return 42.0 * dot(m * m, vec4(
        dot(p0, x0),
        dot(p1, x1),
        dot(p2, x2),
        dot(p3, x3)
    ));
}

vec2 rotate2D(vec2 v, float angle) {
    float s = sin(angle);
    float c = cos(angle);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

vec3 toSpherical(vec3 p) {
    float rho = max(length(p), 0.0001);
    float theta = atan(p.z, p.x);
    float phi = asin(clamp(p.y / rho, -1.0, 1.0));
    return vec3(rho, theta, phi);
}

float accretionDiskNoise(
    vec3 sphericalCoord,
    float time,
    float noiseScale,
    float diskSpeed,
    int noiseLod
) {
    float noise = 1.0;

    for (int i = 0; i < 5; ++i) {
        if (i >= noiseLod) {
            break;
        }

        float octaveScale = float(i * i);
        noise *= 0.5 * snoise(sphericalCoord * octaveScale * noiseScale) + 0.5;

        if ((i % 2) == 0) {
            sphericalCoord.y += time * diskSpeed;
        } else {
            sphericalCoord.y -= time * diskSpeed;
        }
    }

    return abs(noise);
}

void adiskColor(
    vec3 pos,
    float time,
    int noiseLod,
    float stepSize,
    inout vec3 color,
    inout float transmittance
) {
    const float innerRadius = 2.6;
    const float outerRadius = 12.0;

    if (transmittance < 0.003) {
        return;
    }

    float diskHeight = max(objectData.materialCustom.w, 0.015);
    float diskBrightness = max(objectData.materialCustom.z, 0.0);
    float noiseScale = max(objectData.cameraControls.z, 0.0);
    float diskSpeed = max(objectData.cameraControls.w, 0.0);

    if (diskBrightness <= 0.0 ||
        abs(pos.y) >= diskHeight ||
        dot(pos.xz, pos.xz) >= outerRadius * outerRadius) {
        return;
    }

    float radius = length(pos);
    float density = max(
        0.0,
        1.0 - length(pos.xyz / vec3(outerRadius, diskHeight, outerRadius))
    );

    if (density < 0.001) {
        return;
    }

    float verticalDensity = max(1.0 - abs(pos.y) / diskHeight, 0.0);
    density *= verticalDensity * verticalDensity;
    density *= smoothstep(innerRadius, innerRadius * 1.1, radius);

    if (density < 0.001) {
        return;
    }

    vec3 sphericalCoord = vec3(
        radius,
        atan(pos.z, pos.x),
        asin(clamp(pos.y / max(radius, 0.0001), -1.0, 1.0))
    );
    sphericalCoord.y *= 2.0;
    sphericalCoord.z *= 4.0;

    float densityRadius = max(sphericalCoord.x, innerRadius);
    float densityRadius2 = densityRadius * densityRadius;
    density *= 1.0 / (densityRadius2 * densityRadius2);
    density *= 16000.0;

    float noise = accretionDiskNoise(
        sphericalCoord,
        time,
        noiseScale,
        diskSpeed,
        noiseLod
    );

    float seamBlend = smoothstep(1.82 * PI, 2.0 * PI, abs(sphericalCoord.y));
    if (seamBlend > 0.0) {
        vec3 seamCoord = sphericalCoord;
        seamCoord.y += sphericalCoord.y > 0.0 ? -4.0 * PI : 4.0 * PI;
        float seamNoise = accretionDiskNoise(
            seamCoord,
            time,
            noiseScale,
            diskSpeed,
            noiseLod
        );
        noise = mix(noise, 0.5 * (noise + seamNoise), seamBlend);
    }

    vec3 dustColor = textureLod(
        colorMap,
        vec2(saturate(sphericalCoord.x / outerRadius), 0.5),
        0.0
    ).rgb;

    float stepWeight = stepSize / 0.1;
    color += density * diskBrightness * dustColor * transmittance * noise * stepWeight;

    float opticalDepth = min(density * diskBrightness * stepSize * 0.018, 0.24);
    transmittance *= exp(-opticalDepth);
}

vec3 traceColor(vec3 pos, vec3 dir, float time) {
    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    vec3 initialDirection = normalize(dir);

    float renderQuality = clamp(objectData.materialControls.x, 0.35, 1.0);
    float qualityT = saturate((renderQuality - 0.35) / 0.65);
    float stepSize = mix(0.13, 0.06, qualityT);
    int maxSteps = int(mix(240.0, 520.0, qualityT));
    int noiseLod = int(mix(3.0, 5.999, qualityT));
    dir *= stepSize;

    vec3 h = cross(pos, dir);
    float h2 = dot(h, h);
    float lensStrength = max(objectData.materialCustom.y, 0.0);
    float exitRadius2 = max(dot(pos, pos) * 1.15, 14.0 * 14.0);

    for (int i = 0; i < 520; ++i) {
        if (i >= maxSteps) {
            break;
        }

        float radius2 = dot(pos, pos);
        if (radius2 < 0.84) {
            return color;
        }

        if (i > 8 && radius2 > exitRadius2 && dot(pos, dir) > 0.0) {
            break;
        }

        if (lensStrength > 0.001) {
            vec3 acc = accel(h2, pos) * lensStrength;
            dir += acc;
        }

        transmittance *= smootherstep(0.84, 1.08, radius2);
        if (transmittance < 0.003) {
            return color;
        }

        adiskColor(pos, time, noiseLod, stepSize, color, transmittance);
        pos += dir;
    }

    vec3 finalDirection = normalize(dir);
    vec3 skyDirection = rotateVector(finalDirection, vec3(0.0, 1.0, 0.0), time * 0.015);
    float bendAmount = length(finalDirection - initialDirection);
    float lowerHemisphere = smootherstep(0.08, 0.72, -skyDirection.y);
    float skyExposure = max(objectData.materialControls.y, 0.0);
    float skySaturation = max(objectData.materialControls.z, 0.0);
    float skyBlur = max(objectData.materialControls.w, 0.0);
    float skyLod = clamp(skyBlur + bendAmount * 0.58 + lowerHemisphere * 0.28, 0.0, 4.0);
    vec3 skyColor = textureLod(galaxy, skyDirection, skyLod).rgb;
    float skyLuminance = dot(skyColor, vec3(0.2126, 0.7152, 0.0722));
    skyColor = mix(vec3(skyLuminance), skyColor, skySaturation);
    skyColor = mix(skyColor, vec3(skyLuminance), lowerHemisphere * 0.08);
    skyColor *= skyExposure * mix(1.0, 0.94, lowerHemisphere);
    color += skyColor * transmittance;

    return color;
}

vec3 bloomApprox(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 glow = color * smoothstep(0.55, 2.5, luminance);
    return color + glow * 0.08;
}

vec3 tonemap(vec3 color) {
    color = clamp(color, vec3(0.0), vec3(64.0));
    color = bloomApprox(color);
    color = color / (color + vec3(1.0));
    return max(color, vec3(0.0));
}

void main() {
    float time = objectData.materialCustom.x;

    vec2 resolution = max(objectData.viewport.xy, vec2(1.0));
    vec2 uv = gl_FragCoord.xy / resolution.xy - vec2(0.5);
    uv.x *= resolution.x / resolution.y;

    vec3 cameraPos = objectData.cameraPosition.xyz;
    vec3 cameraForward = normalize(objectData.cameraDirection.xyz);
    if (length(cameraForward) < 0.001) {
        cameraForward = normalize(-cameraPos);
    }
    mat3 view = lookAt(cameraPos, cameraPos + cameraForward, 0.0);

    float fovScale = max(objectData.cameraControls.y, 0.15);
    vec3 dir = normalize(vec3(uv.x * fovScale, -uv.y * fovScale, 1.0));
    dir = view * dir;

    vec3 color = traceColor(cameraPos, dir, time);
    color = tonemap(color);

    outColor = vec4(color * objectData.materialBaseColorFactor.rgb, 1.0);
}
