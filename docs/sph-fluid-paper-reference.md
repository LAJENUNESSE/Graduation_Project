# SPH 流体与粒子模拟论文参考

> 检索日期: 2026-05-02
> 检索数据库: arXiv (cs.GR)、Semantic Scholar、OpenAlex
> 检索关键词: SPH, Smoothed Particle Hydrodynamics, Position-Based Fluids, GPU particle, fluid simulation, real-time

---

## 一、基础经典（必读）

| 论文 | 年份 | 引用量 | 要点 |
|------|------|--------|------|
| **Particle-Based Fluid Simulation for Interactive Applications** — Müller, Charypar, Gross (SGP) | 2003 | 2000+ | 首次将 SPH 引入计算机图形学，使用 WCSPH 模型，奠定了实时流体模拟的基础 |
| **Particle-Based Fluid Simulation on GPU** — Amada, Imura, Yasumuro, Manabe, Chihara | 2003 | 93 | 首次在 GPU 上实现 SPH 流体模拟，在可编程图形硬件上达成实时 |
| **Predictive-Corrective Incompressible SPH** — Solenthaler, Pajarola (SIGGRAPH) | 2009 | 800+ | PCISPH: 预测-校正迭代方案，比 WCSPH 大时间步长下密度偏差更小 |
| **SPH Techniques for Physics Based Simulation of Fluids and Solids** — Koschier, Bender, Solenthaler, Teschner (Eurographics Tutorial) | 2019 | — | **SPH 白皮书**。覆盖邻域搜索、压力求解器、边界处理、流体/弹性体/刚体 SPH 全管线 |
| **Position Based Fluids** — Macklin, Müller (SIGGRAPH) | 2013 | 1000+ | PBF 原论文。用迭代约束求解替代压力 Poisson 方程，稳定性极好，适合实时应用 |
| **Implicit Incompressible SPH** — Ihmsen, Cornelis, Solenthaler, Horvath, Teschner (IEEE TVCG) | 2014 | 600+ | IISPH: 隐式压力求解，大时间步长下保持不可压性 |
| **Divergence-Free SPH** — Bender, Koschier (SIGGRAPH) | 2015 | 400+ | DFSPH: 恒定密度 + 零散度速度场，目前最稳定的实时 SPH 方法。迭代数显著低于 PCISPH 和 IISPH |

---

## 二、SPH 方法改进

| 论文 | 年份 | DOI | 要点 |
|------|------|-----|------|
| **Decoupled Boundary Handling in SPH** — Akhunov, Kolb | 2023 | — | 将流-流和流-边界相互作用解耦处理，双密度方案避免边界附近的压力过大估计和异常速度。arXiv: 2306.12277 |
| **Turbulent Details Simulation for SPH Fluids via Vorticity Refinement** — Liu, Wang, Ban, Xu, Zhou, Kosinka, Telea | 2020 | `10.1111/cgf.14095` | 涡度精炼方案: 用流函数关联涡度-速度场，恢复数值耗散丢失的湍流细节，零额外开销，可调强度 |
| **Adaptive Position-Based Fluids: Improving Performance for Real-Time Applications** — Köster, Krüger | 2016 | `10.5121/ijcga.2016.6301` | PBF 细粒度自适应迭代次数调整，大体积海浪场景性能优化 |
| **Implicit Incompressible Porous Flow using SPH** — Böttcher, Jeske, Westhofen, Bender | 2025 | `10.1145/3763325` | 多孔介质 SPH。重叠相密度估计 + 强耦合非压力力（拖曳/浮力/毛细作用），隐式线性系统求解 |
| **Solving Boundary Handling Analytically in 2D for SPH** — Winchenbach, Kolb | 2025 | — | 解析计算三角边界上的 SPH 核函数积分，比传统数值求积快 5 个数量级。Chebyshev 多项式 + 超几何函数求解。arXiv: 2507.21686 |
| **Real-time fluid simulation with adaptive SPH** — Yan, Wang, He, Chen, Wang, Peng | 2009 | `10.1002/cav.300` | 自适应 SPH 模型，根据区域动态调整粒子分辨率，在保持视觉效果的前提下显著加速 |
| **Thin-Film Smoothed Particle Hydrodynamics Fluid** — Wang, Deng, Kong, Prasad, Xiong, Zhu (SIGGRAPH) | 2021 | — | 薄膜 SPH 流体，利用 WCSPH 的可压缩性共同演化膜厚和表面张力。支持瑞利-泰勒不稳定性、毛细波、马兰戈尼效应 |

---

## 三、GPU 加速与实现

| 论文 | 年份 | 引用 | DOI | 要点 |
|------|------|------|-----|------|
| **Real-Time GPU-based SPH Fluid Simulation Using Vulkan and OpenGL Compute Shaders** — Gunadi, Yugopuspito (ICSTC) | 2018 | 12 | `10.1109/ICSTC.2018.8528699` | **与你的项目最接近**。GLSL compute shader → SPIR-V，Vulkan vs OpenGL 4.6 全面对比。Workgroup size=128 最优，Vulkan 在 30K+ 粒子时超越 OpenGL |
| **Moving Towards Large-Scale Particle Based Fluid Simulation in Unity 3D** — Waseem, Hong (Applied Sciences) | 2025 | 0 | `10.3390/app15179706` | **高度相关**。Count Sort + Parallel Prefix Scan 空间哈希 O(n) 邻域搜索、SoA 内存布局比 AoS 快 30–45%、**168,600 particles/ms**、50 万粒子 >30FPS、自适应时间步长仅 2–5% 额外开销 |
| **Fast GPU-Based Fluid Simulations Using SPH** — Krog, Elster (PARA) | 2010 | 35 | `10.1007/978-3-642-28145-7_10` | CUDA 开源 SPH 加速框架，3D CFD，证明 GPU 在复杂 SPH 模型中带宽需求虽大但扩展性良好 |
| **SPH Based Fluid Animation Using CUDA Enabled GPU** — Nuli, Kulkarni | 2012 | 2 | `10.5121/IJCGA.2012.2404` | CUDA SPH 并行化算法形式化描述，相比 CPU 的显著加速证明 |
| **Accelerate Smoothed Particle Hydrodynamics using GPU** — Gao, Wang, Wan, Long | 2010 | 8 | `10.1109/YCICT.2010.5713129` | 早期 GPU-SPH 加速方案 |
| **Fluid simulation method based on CPU-GPU hybrid acceleration** — Huang Peng-fei | 2014 | 0 | — | CPU-GPU 异构混合加速流体模拟方案 |
| **Large scale and interactive fluid simulation and rendering using SPH on GPU** — Brito (UFPE 硕士论文) | 2018 | 0 | — | 弱可压 SPH 大规模模拟 + 交互帧率 + 渲染，覆盖模拟到渲染全管线 |
| **Journey into SPH Simulation: A Comprehensive Framework and Showcase** — Huang, Yi (清华大学) | 2024 | — | — | 完整 SPH 框架开源实现 (GitHub)，集成 WCSPH/PCISPH/DFSPH + 刚流耦合 + 高粘度。CUDA + Taichi 双后端，支持百万粒子。arXiv: 2403.11156 |

---

## 四、渲染与碰撞

| 论文 | 年份 | 引用 | DOI | 要点 |
|------|------|------|-----|------|
| **Real-time high-quality surface rendering for large scale particle-based fluids** — Xiao, Zhang, Yang (I3D) | 2017 | 15 | `10.1145/3023368.3023377` | Screen-space 方案: 粒子 splatting + ray-casting + 法向估计高效组合，不同规模粒子集下验证 |
| **Fluid simulation with rigid body triangle accuracy collision using heterogeneous GPU/CPU** — da Silva, Clua, Pagliosa, Montenegro (I3D) | 2010 | 3 | `10.1145/1730804.1730992` | GPU/CPU 异构架构下的 SPH 刚体三角精度碰撞，混合系统方法 |
| **Particle-Wise Higher-Order SPH Field Approximation for DVR** — Fischer, Schulze, Rosenthal, Linsen | 2024 | — | — | 逐粒子高阶多项式近似 SPH 标量场用于直接体渲染 (DVR)，分辨率自适应 |

---

## 五、前沿方法（神经/学习）

| 论文 | 年份 | 出处 | DOI | 要点 |
|------|------|------|-----|------|
| **Fluid Simulation on Neural Flow Maps** — Deng, Yu, Zhang, Wu, Zhu | 2023 | ACM TOG (SIGGRAPH) | `10.1145/3618392` | Neural Flow Maps: 隐式神经表示 + 流图理论。SSNF 混合神经场表示 + 双向流图 Jacobian，高精度低耗散，能量守恒优于已有方法 |
| **FluidFormer: Transformer with Continuous Convolution for Particle-based Fluid Simulation** — Wang, Chen, Zheng | 2025 | arXiv: 2508.01537 | — | 首个 SPH Transformer。FAB 局部-全局层次注意块，连续卷积提取局部特征 + 自注意力捕获全局依赖，抑制误差积累 |
| **A Pioneering Neural Network Method for Efficient and Robust Fluid Simulation** — Chen, Zheng, Wang, Jin, Chang | 2024 | AAAI-25 | — | 将流体视为点云变换，三角特征融合平衡流体动力学、动量守恒和全局稳定。比 SPH 快 ~10×，比 Flow3D 快 300× |
| **Deep Fluids: A Generative Network for Parameterized Fluid Simulations** — Kim, Azevedo, Thuerey, Kim, Gross, Solenthaler | 2019 | Eurographics | `10.1111/cgf.13619` | 参数化流体模拟的生成模型，散度自由损失函数，速度场重建比 CPU 重模拟快 700×，压缩率 1300× |
| **Efficient Generation of Multimodal Fluid Simulation Data** — Baieri, Crisostomi, Esposito, Maggioli, Rodolà | 2023 | arXiv: 2311.06284 | — | 多模态流体模拟数据集生成框架，用于训练和基准测试神经流体模拟方法 |

---

## 针对本项目的阅读路线

```
第一优先级 (核心实现基础)
├── Koschier et al. 2019  ← SPH 全管线白皮书，实现前必读
├── Gunadi & Yugopuspito 2018 ← 唯一 GLSL compute shader SPH 论文，与你的技术栈一致
└── Macklin & Müller 2013  ← PBF 原论文，密度约束的理论根基

第二优先级 (你的场景痛点直接相关)
├── Akhunov & Kolb 2023  ← 解耦边界处理，解释了边界附近压力异常的根本原因
├── Waseem & Hong 2025  ← SoA + 空间哈希 + 百万粒子实时方案
└── Bender & Koschier 2015 ← DFSPH，如果当前 WCSPH 稳定性不够的首选升级

第三优先级 (效果增强)
├── Liu et al. 2020  ← 涡度精炼，零开销添加湍流细节
├── Xiao et al. 2017  ← 大规模粒子流体 screen-space 表面渲染
└── da Silva et al. 2010 ← 刚体三角精度碰撞，异构架构
```

---

## 检索范围说明

- **arXiv (cs.GR)**: 64 篇匹配，返回前 15 篇按相关性排序
- **Semantic Scholar (Computer Science)**: 422 篇匹配，返回前 15 篇
- **OpenAlex**: 906 篇匹配，按引用量降序返回前 10 篇
- 部分经典论文（Müller 2003, Macklin 2013, Bender 2015, Ihmsen 2014, Solenthaler 2009）为基础领域知识补充，其原始发表渠道非 arXiv

---
