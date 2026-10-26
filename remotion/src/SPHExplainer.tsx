import { Series } from "remotion";
import { Scene, type Caption } from "./components/Scene";
import { Discretization, Intro, MotherFormula } from "./scenes/Opening";
import { DensityAndEOS, Kernels } from "./scenes/KernelsAndDensity";
import { ForceModel } from "./scenes/ForcesAndSearch";
import { GridRebuild } from "./scenes/PCISPHScene";
import { Backends, Stabilization } from "./scenes/Engineering";
import {
  DataLayout,
  FailureModes,
  ParamLayers,
  SolverChoice,
} from "./scenes/DeepDive";
import { Outro, Results } from "./scenes/Results";
import { HashGrid3D, KernelSphere3D } from "./scenes/three/Neighbor3D";
import { FluidColumn3D, PCISPH3D } from "./scenes/three/PCISPH3D";

/**
 * 每个镜头 = 标题 + 场景组件 + 字幕。
 * 时长与文案集中在这里，调整节奏只动这一处。
 */

/** 3D 镜头自行绘制标题与字幕所需的参数 */
type ShotChromeProps = {
  index?: string;
  title?: string;
  anchor?: string;
  captions?: Caption[];
  withChrome?: boolean;
};

type Shot = {
  index: string;
  title: string;
  anchor?: string;
  /** 时长（帧，30fps） */
  duration: number;
  captions: Caption[];
  tone?: "plain" | "tint";
  /**
   * true = 3D 镜头铺满整幅画布，标题与字幕由组件内部叠加渲染；
   * false/缺省 = 常规 2D 版式，由 Scene 提供标题与字幕。
   */
  fullBleed?: boolean;
  Component: React.ComponentType<ShotChromeProps>;
};

export const SHOTS: Shot[] = [
  {
    index: "",
    title: "",
    duration: 150,
    captions: [],
    Component: Intro,
  },
  {
    index: "01",
    title: "拉格朗日离散：不划网格",
    anchor: "Particle / Fluid System",
    duration: 400,
    captions: [
      {
        from: 8,
        text: "SPH 是一种拉格朗日粒子方法：流体被离散成有限个携带物理量的粒子。",
      },
      {
        from: 110,
        text: "每个粒子都带着位置、速度、质量、密度和压力，物理量跟着粒子走。",
      },
      {
        from: 220,
        text: "它不在固定网格上求解，所以不存在网格畸变与重划分的问题，也因此天然适合 GPU 并行。",
      },
      {
        from: 320,
        text: "下一步要解决的问题是：怎么从这堆离散粒子，还原出连续的场？",
      },
    ],
    Component: Discretization,
  },
  {
    index: "02",
    title: "SPH 母式：核函数近似",
    anchor: "SPHKernelMath.h",
    duration: 420,
    captions: [
      {
        from: 10,
        text: "任意场量 A(x) 可以近似成邻域粒子的加权和，这个式子是整套方法的母式。",
      },
      { from: 115, text: "权重由光滑核函数 W 决定，除以 ρ_j 是为了把质量密度归一化。" },
      {
        from: 215,
        text: "支撑域是半径 h 的球——只有落在 h 以内的粒子才参与计算，域外权重为零。",
      },
      {
        from: 320,
        text: "关键推论：每个粒子的计算彼此独立，只依赖邻域数据，这是 GPU Compute 能高效并行的根本原因。",
      },
    ],
    tone: "tint",
    Component: MotherFormula,
  },
  {
    index: "03",
    title: "支撑域是一个球",
    anchor: "SmoothingRadius = h",
    duration: 360,
    captions: [
      {
        from: 10,
        text: "把母式里的支撑域单独拿出来看：它是以粒子为中心、半径 h 的球。",
      },
      {
        from: 120,
        text: "球内粒子的核函数权重 W > 0，参与密度与压力累加；球外权重恒为零，直接跳过。",
      },
      {
        from: 230,
        text: "所以 SPH 从来不需要遍历全部粒子，只需要这个球里的邻居——这正是空间哈希存在的理由。",
      },
    ],
    tone: "tint",
    fullBleed: true,
    Component: KernelSphere3D,
  },
  {
    index: "04",
    title: "核函数：Poly6 与 Spiky 的分工",
    anchor: "SPHKernelMath::Compute()",
    duration: 450,
    captions: [
      {
        from: 10,
        text: "密度用 Poly6：它在 r=0 附近平滑，密度求和不会因为邻粒子贴近而剧烈跳变。",
      },
      {
        from: 120,
        text: "压力用 Spiky 梯度：它在 r→0 处最陡，短程排斥力足够强，能防止粒子互相穿插。",
      },
      {
        from: 230,
        text: "为什么不直接对 Poly6 求梯度？因为它的梯度在 r→0 时趋于零，给不出足够的排斥力。",
      },
      {
        from: 340,
        text: "两项系数 315/(64πh⁹) 与 −45/(πh⁶) 都在 CPU 侧预计算，GPU 每个邻居只做一次乘法。",
      },
    ],
    Component: Kernels,
  },
  {
    index: "05",
    title: "密度估计与状态方程",
    anchor: "sph_density.glsl",
    duration: 450,
    captions: [
      { from: 10, text: "密度是对邻域做 Poly6 加权求和，粒子自身也贡献一项 W(0,h)。" },
      {
        from: 115,
        text: "有了密度，用状态方程 P = max(0, k(ρ − ρ₀)) 得到压力：只有密度超过静止密度才产生正压力。",
      },
      {
        from: 225,
        text: "看图右侧的读数：ρ 从 1000 涨到 1240 时，压力从 0 开始随密度差线性上升。",
      },
      {
        from: 335,
        text: "压力钳到非负是工程选择：允许负压力会让粒子互吸，出现粘团、空洞塌陷和高频振荡。",
      },
    ],
    tone: "tint",
    Component: DensityAndEOS,
  },
  {
    index: "06",
    title: "WCSPH 的力模型",
    anchor: "sph_force.glsl",
    duration: 480,
    captions: [
      {
        from: 10,
        text: "压力力用 (Pi+Pj)/2 的对称化形式，避免单边压力带来的偏置，让粒子间相互作用更平滑。",
      },
      { from: 120, text: "粘性力让速度场扩散，抑制噪声；系数太大会出现明显的果冻拖拽感。" },
      {
        from: 230,
        text: "表面张力用 Akinci 模型，法线在 density pass 先算好，force pass 直接读取，避免重复遍历邻域。",
      },
      {
        from: 345,
        text: "刚体边界是 SDF 惩罚力而不是硬约束：按穿透深度给排斥，按相对法向速度给阻尼。",
      },
    ],
    Component: ForceModel,
  },
  {
    index: "07",
    title: "邻居搜索：27 个格子的三维邻域",
    anchor: "SpatialHashGrid",
    duration: 480,
    captions: [
      {
        from: 10,
        text: "朴素做法是每个粒子跟全部粒子比距离，复杂度 O(N²)，上万粒子就撑不住。",
      },
      {
        from: 120,
        text: "空间哈希把三维格坐标映射成一维下标，取模回卷处理越界，查询退化成 O(1)。",
      },
      {
        from: 235,
        text: "注意这里是 3×3×3 共 27 个格子，不是平面上看到的 9 个——邻居遍历量是它的三倍。",
      },
      {
        from: 350,
        text: "计数 → 前缀和 → 散射三步建表；取 cellSize = 2h，27 格就能完整覆盖半径 h 的支撑球。",
      },
    ],
    tone: "tint",
    fullBleed: true,
    Component: HashGrid3D,
  },
  {
    index: "08",
    title: "PCISPH：把压力变成迭代",
    anchor: "sph_pcisph_*.glsl",
    duration: 540,
    captions: [
      {
        from: 10,
        text: "WCSPH 的压力一次算完，不可压缩性只是近似。时间步偏大或刚体耦合强时，密度波动会很明显。",
      },
      {
        from: 125,
        text: "看这柱粒子压向地面：被压扁意味着局部密度已经超过 ρ₀，颜色随之变红。",
      },
      {
        from: 240,
        text: "PCISPH 改用预测-校正：用预测位置重新估密度，再按 P ← P + δ·Δρ 逐轮把压力补上去。",
      },
      {
        from: 355,
        text: "每一轮迭代都把体积往回收一点，右侧误差条可以看到它逐步收敛。",
      },
      {
        from: 460,
        text: "迭代次数 I 直接决定精度与耗时，仓库默认 3 次，可在 1~8 之间调。",
      },
    ],
    fullBleed: true,
    Component: PCISPH3D,
  },
  {
    index: "09",
    title: "校正后的流体形态",
    anchor: "WCSPH 力模型 · 三维演示",
    duration: 380,
    captions: [
      {
        from: 10,
        text: "把同样的力模型放回三维：重力把水柱拉塌，近邻排斥让它向外铺开。",
      },
      {
        from: 130,
        text: "粘性让邻近粒子的速度趋于一致，所以流动是连成片的，而不是各自飞散。",
      },
      {
        from: 260,
        text: "粒子越压越密的地方压力越大——这就是状态方程在三维场景里最直观的体现。",
      },
    ],
    fullBleed: true,
    Component: FluidColumn3D,
  },
  {
    index: "10",
    title: "δ 与网格重建的取舍",
    anchor: "ComputePCISPHDelta()",
    duration: 420,
    captions: [
      {
        from: 10,
        text: "δ 是密度误差到压力增量的增益系数，由 ComputePCISPHDelta() 按 Solenthaler 2009 推导。",
      },
      {
        from: 120,
        text: "推导假设立方格点排列，初始间距 ≈ (m/ρ₀)^(1/3)，累加 Spiky 梯度得到 β ∝ Δt²m²，δ ≈ −1/β。",
      },
      {
        from: 235,
        text: "δ 太小修正不足、迭代不收敛；太大会压力过冲、振荡甚至爆炸。它不是越大越稳。",
      },
      {
        from: 340,
        text: "另一个取舍是 grid：每轮重建最准但最贵，始终不重建会失真，仓库让迭代 0 复用、迭代 1 起重建。",
      },
    ],
    tone: "tint",
    Component: GridRebuild,
  },
  {
    index: "11",
    title: "工程稳定化措施",
    anchor: "SPHCommon.cpp / Components.h",
    duration: 450,
    captions: [
      {
        from: 10,
        text: "公式写对不等于跑得稳。这一页是让它在真实场景里不炸、不闪、不粘团的具体手段。",
      },
      { from: 120, text: "density 下限保护防止接近 0 的分母；加速度限幅在极端参数下保住系统。" },
      {
        from: 230,
        text: "新生粒子 warm-up 只对压力项渐入，解决 burst 发射瞬间粒子被弹飞的闪烁。",
      },
      {
        from: 340,
        text: "PCISPH 路径把 simulate 里的重力置零，因为预测/修正阶段已经算过，再叠加就是双算。",
      },
    ],
    Component: Stabilization,
  },
  {
    index: "12",
    title: "参数不是平行滑条",
    anchor: "Components.h / PropertiesPanel",
    duration: 480,
    captions: [
      {
        from: 10,
        text: "十几个参数不是平行的滑条，按三层结构看才不会乱：尺度、刚度、风格。",
      },
      {
        from: 120,
        text: "尺度参数互相强耦合：改 m 或 ρ₀ 等于改变隐含的粒子间距，同时牵动核系数、cellSize 和 δ。",
      },
      {
        from: 235,
        text: "刚度参数决定流体有多「硬」，δ 与 Iterations 是同一套校正系统的两个旋钮，不要同时激进。",
      },
      {
        from: 350,
        text: "推荐的调参顺序是先定尺度、再定刚度、最后调风格——顺序反了会反复推倒重来。",
      },
    ],
    tone: "tint",
    Component: ParamLayers,
  },
  {
    index: "13",
    title: "GPU 数据布局",
    anchor: "ParticleSystemGPU.cpp",
    duration: 450,
    captions: [
      { from: 10, text: "单个粒子是 5 个 vec4，共 80 字节，物理量与渲染字段打包在同一结构里。" },
      {
        from: 120,
        text: "密度和压力塞进 params.zw：复用已有的颜色/尺寸字段，不额外开密度缓冲，省显存也省带宽容。",
      },
      {
        from: 235,
        text: "aliveIndices 提供紧凑的存活列表，sortedIndices 提供按格排序的邻域遍历序，职责不同不能合并。",
      },
      {
        from: 345,
        text: "SPH 参数对所有粒子只读，所以走 UBO / 常量内存，命中率比全局内存更高。",
      },
    ],
    Component: DataLayout,
  },
  {
    index: "14",
    title: "双后端一致性",
    anchor: "CudaSPHPipeline.h",
    duration: 420,
    captions: [
      {
        from: 10,
        text: "CUDA 侧暴露了与 GL pass 一一对应的入口，不是另写一套物理模型。",
      },
      { from: 120, text: "真正的差异只在执行后端、scan 与 kernel 并行实现、以及性能特性。" },
      {
        from: 230,
        text: "正因为数学语义一致，「加速比」这个数字才有比较意义；CUDA 出错时自动回退到 OpenGL。",
      },
      {
        from: 335,
        text: "这种一致性不是自然得到的，需要两边共享同一套刚体缓冲区布局并逐项对齐 binding。",
      },
    ],
    tone: "tint",
    Component: Backends,
  },
  {
    index: "15",
    title: "实测：三后端 × 两求解器 × 五规模",
    anchor: "benchmark/results",
    duration: 480,
    captions: [
      {
        from: 10,
        text: "PCISPH 负载下 CUDA 全规模快于 OpenGL 基线，50000 粒子档单帧 90.7 ms 降到 75.8 ms。",
      },
      {
        from: 130,
        text: "WCSPH 属轻负载，加速比 0.99×～1.55×；20000 档与基线基本持平，50000 档升至 1.55×。",
      },
      {
        from: 250,
        text: "性能之前先过正确性门禁：冻结密度偏差容差，跨后端平均密度最大相对偏差 0.0261%。",
      },
      {
        from: 370,
        text: "先证明三后端算的是同一套物理，再比较快慢——这是这份数据可信的前提。",
      },
    ],
    Component: Results,
  },
  {
    index: "16",
    title: "两种求解器怎么选",
    anchor: "WCSPH vs PCISPH",
    duration: 420,
    captions: [
      { from: 10, text: "同一套 SPH 骨架，压力求解策略不同，适用场景也不同。" },
      {
        from: 130,
        text: "WCSPH 便宜，适合大规模视觉粒子；PCISPH 贵，但体积守恒更好，适合刚体耦合强的场景。",
      },
      {
        from: 250,
        text: "50k 档的耗时差距不是实现优劣，而是算法复杂度差异——PCISPH 每帧多跑 I 次密度与压力重算。",
      },
    ],
    Component: SolverChoice,
  },
  {
    index: "17",
    title: "常见失败模式与排查",
    anchor: "调参手册",
    duration: 420,
    captions: [
      { from: 10, text: "调试时先认现象，再反查参数，而不是随机拖滑条。" },
      {
        from: 130,
        text: "爆炸优先查 δ、Δt 和密度分母；粘团查负压力和粘性；边界抖动查刚度和阻尼；闪烁查 warm-up 与 pass 屏障。",
      },
      {
        from: 260,
        text: "每一个现象都对应前文某条稳定化措施失效后的具体表现，不是玄学。",
      },
    ],
    tone: "tint",
    Component: FailureModes,
  },
  {
    index: "",
    title: "",
    duration: 300,
    captions: [
      {
        from: 120,
        text: "从离散、核函数、密度、压力，到迭代校正与 GPU 并行——这是本项目 SPH 部分的完整链路。",
      },
    ],
    Component: Outro,
  },
];

/** 总时长，Root 里用它注册 Composition */
export const TOTAL_DURATION = SHOTS.reduce(
  (sum, shot) => sum + shot.duration,
  0,
);

export const SPHExplainer: React.FC = () => {
  return (
    <Series>
      {SHOTS.map((shot, i) => (
        <Series.Sequence key={i} durationInFrames={shot.duration}>
          {shot.fullBleed ? (
            // 3D 镜头：铺满整幅，标题与字幕由组件自身叠加
            <shot.Component
              index={shot.index}
              title={shot.title}
              anchor={shot.anchor}
              captions={shot.captions}
              withChrome
            />
          ) : (
            <Scene
              index={shot.index}
              title={shot.title}
              anchor={shot.anchor}
              captions={shot.captions}
              tone={shot.tone}
            >
              <shot.Component />
            </Scene>
          )}
        </Series.Sequence>
      ))}
    </Series>
  );
};
