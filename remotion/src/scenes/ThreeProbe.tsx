import { ThreeCanvas } from "@remotion/three";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { VIDEO } from "../theme";

/**
 * 3D 能力探针。
 *
 * 用途：确认 headless 软件 WebGL 下能否正常渲染 Three.js 场景。
 * Remotion 默认 hardwareAcceleration: "disable"，若立体渲染在软件
 * 光栅化下不可用，必须先通过这个探针发现，而不是等整片渲染完才发现全黑。
 */
export const ThreeProbe: React.FC = () => {
  const frame = useCurrentFrame();
  const angle = (frame / 90) * Math.PI * 2;

  return (
    <AbsoluteFill style={{ backgroundColor: "#1A1A1A" }}>
      <ThreeCanvas
        width={VIDEO.width}
        height={VIDEO.height}
        camera={{ position: [0, 0, 6], fov: 45 }}
        style={{ backgroundColor: "#1A1A1A" }}
      >
        <ambientLight intensity={0.6} />
        <directionalLight position={[4, 6, 5]} intensity={2.2} />
        <mesh rotation={[0.5, angle, 0]}>
          <boxGeometry args={[2, 2, 2]} />
          <meshStandardMaterial color="#2E6FA8" roughness={0.35} metalness={0.1} />
        </mesh>
        <mesh position={[3, -2, -1]}>
          <sphereGeometry args={[0.9, 32, 32]} />
          <meshStandardMaterial color="#C4661E" roughness={0.4} />
        </mesh>
        <gridHelper args={[10, 10, "#444444", "#2A2A2A"]} position={[0, -2.5, 0]} />
      </ThreeCanvas>
    </AbsoluteFill>
  );
};
