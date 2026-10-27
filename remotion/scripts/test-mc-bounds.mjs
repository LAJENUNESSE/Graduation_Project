/**
 * 检查 MarchingCubes 生成的几何体真实包围盒。
 *
 * 渲染全空但顶点数满额，怀疑几何体尺度极小或位置偏移到视野外。
 * 这里直接算包围盒，确认它的实际空间范围。
 *
 * 用法：node scripts/test-mc-bounds.mjs
 */
import * as THREE from "three";
import { MarchingCubes } from "three/examples/jsm/objects/MarchingCubes.js";

const material = new THREE.MeshNormalMaterial();
const mc = new MarchingCubes(40, material, false, false, 80000);
mc.isolation = 40;
mc.reset();
for (let i = 0; i < 10; i++) {
  const a = (i / 10) * Math.PI * 2;
  mc.addBall(
    0.5 + Math.cos(a) * 0.08,
    0.5,
    0.5 + Math.sin(a) * 0.08,
    0.9,
    12,
  );
}
mc.update();

const geo = mc.geometry;
const pos = geo.getAttribute("position");
console.log("position count:", pos.count);
console.log("drawRange:", JSON.stringify(geo.drawRange));

// 只看 drawRange 内的真实顶点
let minX = Infinity,
  minY = Infinity,
  minZ = Infinity;
let maxX = -Infinity,
  maxY = -Infinity,
  maxZ = -Infinity;
let nonZero = 0;
for (let i = 0; i < pos.count; i++) {
  const x = pos.getX(i);
  const y = pos.getY(i);
  const z = pos.getZ(i);
  if (x !== 0 || y !== 0 || z !== 0) nonZero++;
  if (x < minX) minX = x;
  if (y < minY) minY = y;
  if (z < minZ) minZ = z;
  if (x > maxX) maxX = x;
  if (y > maxY) maxY = y;
  if (z > maxZ) maxZ = z;
}
console.log("非零顶点数:", nonZero);
console.log(`包围盒: x[${minX.toFixed(3)}, ${maxX.toFixed(3)}] y[${minY.toFixed(3)}, ${maxY.toFixed(3)}] z[${minZ.toFixed(3)}, ${maxZ.toFixed(3)}]`);
console.log("scale:", mc.scale.x, "position:", mc.position.toArray().join(","));
