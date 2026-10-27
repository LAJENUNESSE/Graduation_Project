import { ThreeCanvas } from "@remotion/three";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { useMemo } from "react";
import * as THREE from "three";
import { VIDEO } from "../../theme";

/**
 * 光线步进 metaball 探针。
 *
 * MarchingCubes 在 headless 软件 WebGL 下画不出来（几何生成了但不出像素），
 * 改用片元着色器直接在密度场里做 sphere tracing：
 *   - 液面由隐式场求出，天然连续光滑，没有「粒子球」感
 *   - 法线由场梯度求得，可以做出真实的 Fresnel / 折射 / 吸收
 * 这也更接近项目里 SSFR 的合成思路。
 */
const vertexShader = /* glsl */ `
  varying vec3 vWorldPos;
  void main() {
    vec4 wp = modelMatrix * vec4(position, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = projectionMatrix * viewMatrix * wp;
  }
`;

const fragmentShader = /* glsl */ `
  precision highp float;

  varying vec3 vWorldPos;
  uniform vec3 uCamPos;
  uniform float uTime;

  // 平滑并集：把多个球融合成一团连续液面
  float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
  }

  // 密度场：一组会轻微晃动的球
  float map(vec3 p) {
    float d = 1e9;
    for (int i = 0; i < 8; i++) {
      float fi = float(i);
      float a = fi * 0.785 + uTime * 0.6;
      vec3 c = vec3(cos(a) * 0.34, sin(a * 1.7 + uTime) * 0.14, sin(a) * 0.34);
      float r = 0.46 + 0.06 * sin(uTime * 1.3 + fi);
      d = smin(d, length(p - c) - r, 0.28);
    }
    // 地面：y = -1.0，做成光滑容器底部
    d = smin(d, p.y + 1.0, 0.12);
    return d;
  }

  vec3 calcNormal(vec3 p) {
    vec2 e = vec2(0.0015, 0.0);
    return normalize(vec3(
      map(p + e.xyy) - map(p - e.xyy),
      map(p + e.yxy) - map(p - e.yxy),
      map(p + e.yyx) - map(p - e.yyx)
    ));
  }

  void main() {
    vec3 ro = uCamPos;
    vec3 rd = normalize(vWorldPos - uCamPos);

    // sphere tracing
    float t = 0.0;
    bool hit = false;
    for (int i = 0; i < 96; i++) {
      vec3 p = ro + rd * t;
      float d = map(p);
      if (d < 0.0015) { hit = true; break; }
      t += max(d, 0.004);
      if (t > 14.0) break;
    }

    // 背景：柔和渐变
    vec3 bg = mix(vec3(0.93, 0.93, 0.90), vec3(0.80, 0.85, 0.88),
                  clamp(gl_FragCoord.y / 1080.0, 0.0, 1.0));

    if (!hit) {
      gl_FragColor = vec4(bg, 1.0);
      return;
    }

    vec3 p = ro + rd * t;
    vec3 n = calcNormal(p);
    vec3 v = -rd;

    // Fresnel（Schlick）
    float cosT = clamp(dot(n, v), 0.0, 1.0);
    float fres = 0.02 + 0.98 * pow(1.0 - cosT, 5.0);

    // 主光 + 环境
    vec3 lightDir = normalize(vec3(0.45, 0.85, 0.35));
    float diff = clamp(dot(n, lightDir), 0.0, 1.0);
    vec3 halfV = normalize(lightDir + v);
    float spec = pow(clamp(dot(n, halfV), 0.0, 1.0), 96.0);

    // 水体颜色 + 厚度近似（沿法线反向再采一次，粗略表示透射深度）
    float thick = 0.0;
    for (int i = 1; i <= 6; i++) {
      vec3 sp = p - n * float(i) * 0.06;
      thick += (map(sp) < 0.0) ? 1.0 : 0.0;
    }
    thick /= 6.0;
    vec3 deep = vec3(0.09, 0.32, 0.52);
    vec3 shallow = vec3(0.35, 0.68, 0.82);
    vec3 body = mix(shallow, deep, thick);

    // 合成：漫反射 + 高光 + Fresnel 反射天光
    vec3 col = body * (0.35 + 0.75 * diff);
    col += vec3(1.0) * spec * 0.9;
    col = mix(col, bg * 1.05, fres * 0.55);

    // 边缘轻微提亮，模拟小液滴透光
    float rim = pow(1.0 - cosT, 3.0);
    col += vec3(0.16, 0.34, 0.42) * rim * 0.6;

    gl_FragColor = vec4(pow(col, vec3(0.92)), 1.0);
  }
`;

export const MetaballProbe: React.FC = () => {
  const frame = useCurrentFrame();

  const material = useMemo(
    () =>
      new THREE.ShaderMaterial({
        vertexShader,
        fragmentShader,
        uniforms: {
          uCamPos: { value: new THREE.Vector3(0, 0.55, 2.9) },
          uTime: { value: 0 },
        },
        side: THREE.DoubleSide,
      }),
    [],
  );

  // 每帧更新动画相位
  material.uniforms.uTime.value = frame / 30;

  return (
    <AbsoluteFill style={{ backgroundColor: "#E9E8E2" }}>
      <ThreeCanvas
        width={VIDEO.width}
        height={VIDEO.height}
        camera={{ position: [0, 0.55, 2.9], fov: 45 }}
        style={{ backgroundColor: "#E9E8E2" }}
      >
        <mesh material={material} frustumCulled={false}>
          <boxGeometry args={[9, 9, 9]} />
        </mesh>
      </ThreeCanvas>
    </AbsoluteFill>
  );
};
