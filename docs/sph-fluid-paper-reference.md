# SPH 流体与粒子模拟论文参考

> 检索日期: 2026-05-02
> 检索数据库: arXiv (cs.GR)、Semantic Scholar、OpenAlex
> 检索关键词: SPH, Smoothed Particle Hydrodynamics, Position-Based Fluids, GPU particle, fluid simulation, real-time
> 参考文献格式: GB/T 7714-2015

---

## 一、基础经典（必读）

[1] MÜLLER M, CHARYPAR D, GROSS M. Particle-based fluid simulation for interactive applications[C]//Proceedings of the 2003 ACM SIGGRAPH/Eurographics Symposium on Computer Animation (SCA '03). San Diego, California: Eurographics Association, 2003: 154-159.

[2] AMADA T, IMURA M, YASUMURO Y, et al. Particle-based fluid simulation on GPU[C]//ACM Workshop on General-Purpose Computing on Graphics Processors. Los Angeles, 2004.

[3] SOLENTHALER B, PAJAROLA R. Predictive-corrective incompressible SPH[J]. ACM Transactions on Graphics, 2009, 28(3): 40.

[4] KOSCHIER D, BENDER J, SOLENTHALER B, et al. Smoothed particle hydrodynamics techniques for the physics based simulation of fluids and solids[C]//Eurographics 2019 — Tutorials. Genoa, Italy: Eurographics Association, 2019. DOI: 10.2312/egt.20191035.

[5] MACKLIN M, MÜLLER M. Position based fluids[J]. ACM Transactions on Graphics, 2013, 32(4): 104.

[6] IHMSEN M, CORNELIS J, SOLENTHALER B, et al. Implicit incompressible SPH[J]. IEEE Transactions on Visualization and Computer Graphics, 2014, 20(3): 426-435.

[7] BENDER J, KOSCHIER D. Divergence-free smoothed particle hydrodynamics[C]//Proceedings of the 14th ACM SIGGRAPH/Eurographics Symposium on Computer Animation (SCA '15). Los Angeles, California: ACM, 2015: 147-155.

---

## 二、SPH 方法改进

[8] AKHUNOV R, KOLB A. Decoupled boundary handling in SPH[EB/OL]. (2023-06-21)[2026-05-02]. https://arxiv.org/abs/2306.12277.

[9] LIU S, WANG X, BAN X, et al. Turbulent details simulation for SPH fluids via vorticity refinement[J]. Computer Graphics Forum, 2020, 39(7): 167-178. DOI: 10.1111/cgf.14095.

[10] KÖSTER M, KRÜGER A. Adaptive position-based fluids: improving performance of fluid simulations for real-time applications[J]. International Journal of Computer Graphics & Animation, 2016, 6(3): 1-16. DOI: 10.5121/ijcga.2016.6301.

[11] BÖTTCHER T, JESKE S R, WESTHOFEN L, et al. Implicit incompressible porous flow using SPH[J]. ACM Transactions on Graphics, 2025. DOI: 10.1145/3763325.

[12] WINCHENBACH R, KOLB A. Solving boundary handling analytically in two dimensions for smoothed particle hydrodynamics[EB/OL]. (2025-07-29)[2026-05-02]. https://arxiv.org/abs/2507.21686.

[13] YAN H, WANG Z, HE J, et al. Real-time fluid simulation with adaptive SPH[J]. Computer Animation and Virtual Worlds, 2009, 20(2-3): 217-226. DOI: 10.1002/cav.300.

[14] WANG M, DENG Y, KONG X, et al. Thin-film smoothed particle hydrodynamics fluid[J]. ACM Transactions on Graphics, 2021, 40(4): 98.

---

## 三、GPU 加速与实现

[15] GUNADI S I, YUGOPUSPITO P. Real-time GPU-based SPH fluid simulation using Vulkan and OpenGL compute shaders[C]//Proceedings of the 4th International Conference on Science and Technology (ICSTC 2018). Yogyakarta, Indonesia: IEEE, 2018: 1-6. DOI: 10.1109/ICSTC.2018.8528699.

[16] WASEEM M, HONG M. Moving towards large-scale particle based fluid simulation in Unity 3D[J]. Applied Sciences, 2025, 15(17): 9706. DOI: 10.3390/app15179706.

[17] KROG Ø E, ELSTER A C. Fast GPU-based fluid simulations using SPH[C]//Applied Parallel and Scientific Computing (PARA 2010): Part I. Reykjavík, Iceland: Springer, 2012: 98-109. DOI: 10.1007/978-3-642-28145-7_10.

[18] NULI U A, KULKARNI P. SPH based fluid animation using CUDA enabled GPU[J]. International Journal of Computer Graphics & Animation, 2012, 2(4): 45-53. DOI: 10.5121/ijcga.2012.2404.

[19] GAO X, WANG Z, WAN H, et al. Accelerate smoothed particle hydrodynamics using GPU[C]//Proceedings of the 2nd Youth Conference on Information and Communication Technology (YCICT 2010). 2010: 31-34. DOI: 10.1109/YCICT.2010.5713129.

[20] HUANG P F. Fluid simulation method based on CPU-GPU hybrid acceleration[D]. 2014.

[21] BRITO C J S. Large scale and interactive fluid simulation and rendering using the smoothed particle hydrodynamics technique on GPU[D]. Recife: Universidade Federal de Pernambuco, 2018.

[22] HUANG H, YI L. Journey into SPH simulation: a comprehensive framework and showcase[EB/OL]. (2024-03-17)[2026-05-02]. https://arxiv.org/abs/2403.11156.

---

## 四、渲染与碰撞

[23] XIAO X, ZHANG S, YANG X. Real-time high-quality surface rendering for large scale particle-based fluids[C]//Proceedings of the 21st ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games (I3D '17). San Francisco, California: ACM, 2017: 1-8. DOI: 10.1145/3023368.3023377.

[24] DA SILVA J R, CLUA E, PAGLIOSA P, et al. Fluid simulation with rigid body triangle accuracy collision using an heterogeneous GPU/CPU hardware system[C]//Proceedings of the 2010 ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games (I3D '10). Washington, D.C.: ACM, 2010: 157-164. DOI: 10.1145/1730804.1730992.

[25] FISCHER J, SCHULZE M, ROSENTHAL P, et al. Particle-wise higher-order SPH field approximation for DVR[EB/OL]. (2024-01-05)[2026-05-02]. https://arxiv.org/abs/2401.02896.

---

## 五、前沿方法（神经/学习）

[26] DENG Y, YU H X, ZHANG D, et al. Fluid simulation on neural flow maps[J]. ACM Transactions on Graphics, 2023, 42(6): 248. DOI: 10.1145/3618392.

[27] WANG N, CHEN Y, ZHENG S. FluidFormer: transformer with continuous convolution for particle-based fluid simulation[EB/OL]. (2025-08-03)[2026-05-02]. https://arxiv.org/abs/2508.01537.

[28] CHEN Y, ZHENG S, WANG N, et al. A pioneering neural network method for efficient and robust fluid simulation[C]//Proceedings of the 39th AAAI Conference on Artificial Intelligence (AAAI-25). Philadelphia, Pennsylvania: AAAI Press, 2025.

[29] KIM B, AZEVEDO V C, THUEREY N, et al. Deep fluids: a generative network for parameterized fluid simulations[J]. Computer Graphics Forum, 2019, 38(2): 59-70. DOI: 10.1111/cgf.13619.

[30] BAIERI D, CRISOSTOMI D, ESPOSITO S, et al. Efficient generation of multimodal fluid simulation data[EB/OL]. (2023-12-22)[2026-05-02]. https://arxiv.org/abs/2311.06284.

---

## 针对本项目的阅读路线

```
第一优先级 (核心实现基础)
├── [4] Koschier et al. 2019  ← SPH 全管线白皮书，实现前必读
├── [15] Gunadi & Yugopuspito 2018 ← 唯一 GLSL compute shader SPH 论文，与你的技术栈一致
└── [5] Macklin & Müller 2013  ← PBF 原论文，密度约束的理论根基

第二优先级 (你的场景痛点直接相关)
├── [8] Akhunov & Kolb 2023  ← 解耦边界处理，解释了边界附近压力异常的根本原因
├── [16] Waseem & Hong 2025  ← SoA + 空间哈希 + 百万粒子实时方案
└── [7] Bender & Koschier 2015 ← DFSPH，当前 WCSPH 稳定性不够时的首选升级

第三优先级 (效果增强)
├── [9] Liu et al. 2020  ← 涡度精炼，零开销添加湍流细节
├── [23] Xiao et al. 2017  ← 大规模粒子流体 screen-space 表面渲染
└── [24] da Silva et al. 2010 ← 刚体三角精度碰撞，异构架构
```

---

## 检索范围说明

- **arXiv (cs.GR)**: 64 篇匹配，返回前 15 篇按相关性排序
- **Semantic Scholar (Computer Science)**: 422 篇匹配，返回前 15 篇
- **OpenAlex**: 906 篇匹配，按引用量降序返回前 10 篇
- 部分经典论文（[1] Müller 2003, [5] Macklin 2013, [7] Bender 2015, [6] Ihmsen 2014, [3] Solenthaler 2009）为基础领域知识补充，其原始发表渠道未纳入上述数据库检索结果
