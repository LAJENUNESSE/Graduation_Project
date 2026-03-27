# Vulkan-CUDA External Interop Demo

本目录提供一期独立验证 Demo：`External Memory + Timeline Semaphore`。

## 目标

- 验证 CUDA 写入 Vulkan 导出的粒子缓冲。
- 验证固定同步协议：
  - CUDA 等待上帧 Vulkan timeline 值
  - CUDA kernel 执行并 signal
  - Vulkan submit 等待 CUDA signal，渲染后回写 timeline
- 输出统一 CSV 诊断日志，覆盖标定与正式运行两阶段。

## 前置条件

- `ENGINE_ENABLE_CUDA=ON`
- Vulkan 可用（包含开发头/库，`find_package(Vulkan)` 可通过）
- NVIDIA CUDA 驱动与 Toolkit 可用

若 Vulkan 能力不足，Demo 会明确报错退出；不会降级到旧的 `cudaGraphicsMapResources/UnmapResources` 路径。

## 构建（Windows）

```powershell
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --preset vs2022-cuda
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config RelWithDebInfo --target VulkanCudaInteropDemo
```

## 运行参数

- `--particles=<N>`: 固定粒子数（仅在关闭标定时作为最终负载）
- `--pcisph-iters=<N>`: 迭代次数
- `--calibrate=<0|1>`: 是否开启自动标定（默认开启）
- `--no-calibrate`: 关闭自动标定
- `--duration-sec=<N>`: 运行秒数（默认 180）
- `--vsync=<0|1>`: 是否开启 VSync

默认标定档位：`50k -> 75k -> 100k -> 150k -> 200k`。

## 诊断日志

运行后会生成：`logs/vk_cuda_interop_YYYYmmdd_HHMMSS.csv`

关键字段包括：

- CUDA: `CudaWaitMs`, `CudaKernelMs`, `CudaSignalMs`
- Vulkan: `VkAcquireMs`, `VkSubmitMs`, `VkPresentMs`
- Sync: `WaitVulkanValue`, `CudaSignalValue`, `VkSignalValue`
- Calibration: `CalibrationP95Ms`, `CalibrationMaxMs`, `CalibrationSpikeCount`
- 结果: `Status`, `Error`

## 验收建议

1. 开启默认标定运行一次，确认可选出稳定档。
2. 在稳定档连续运行 `180s`，确认无 CUDA/Vulkan 错误、无死锁。
3. 检查 CSV：确认同步值单调递增且无异常跳变。