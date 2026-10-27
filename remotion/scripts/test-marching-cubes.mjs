/**
 * 在 Node 侧单独验证 MarchingCubes 的密度场与等值面参数。
 *
 * 目的：把「Three.js 渲染问题」和「参数问题」分开。
 * 如果这里能生成顶点，说明 headless 渲染全空是材质/光照/相机的问题；
 * 如果这里也是 0 顶点，说明 isolation 与 addBall 的强度不匹配。
 *
 * 用法：node scripts/test-marching-cubes.mjs
 */
import * as THREE from "three";
import { MarchingCubes } from "three/examples/jsm/objects/MarchingCubes.js";

const material = new THREE.MeshStandardMaterial();

const tryConfig = (res, isolation, strength, subtract, label) => {
  const mc = new MarchingCubes(res, material, false, false, 80000);
  mc.isolation = isolation;
  mc.reset();
  // 中心一团球
  for (let i = 0; i < 10; i++) {
    const a = (i / 10) * Math.PI * 2;
    mc.addBall(
      0.5 + Math.cos(a) * 0.08,
      0.5,
      0.5 + Math.sin(a) * 0.08,
      strength,
      subtract,
    );
  }
  mc.update();
  const count = mc.geometry.getAttribute("position")?.count ?? 0;
  console.log(
    `${label}: res=${res} isolation=${isolation} strength=${strength} subtract=${subtract} → verts=${count}`,
  );
  return count;
};

console.log("=== 扫描参数，找出能生成顶点的组合 ===");
tryConfig(40, 120, 0.9, 12, "A");
tryConfig(40, 80, 0.9, 12, "B");
tryConfig(40, 40, 0.9, 12, "C");
tryConfig(40, 20, 0.9, 12, "D");
tryConfig(64, 40, 0.8, 20, "E");
tryConfig(64, 20, 0.8, 20, "F");
tryConfig(32, 10, 0.6, 10, "G");
