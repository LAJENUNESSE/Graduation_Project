# 测试说明文档

## 1. 本次新增测试的目标与范围
本次测试补充的主要目标是在不修改现有项目核心依赖的情况下，验证资源管理模块中低依赖的纯逻辑工具类的正确性。
测试范围涵盖了资源句柄 (`AssetHandle`) 和资源容器 (`SlotMap`) 的基本行为，验证其边界条件及主要功能，包括有效性检查、等价性判定、容器插入、替换、删除及清空功能，以及在元素被移除后对句柄“代际”（Generation）失效的响应。

## 2. 为什么优先选择这些模块补测
由于本项目是一个包含渲染、音频、视频解码及物理模拟（如 OpenGL、OpenAL、FFmpeg、Bullet）的重度 C++ 引擎项目，如果直接在不具备合适设备或者缺乏完整上下文配置的环境中编写依赖这些子系统的重型集成测试，将导致测试无法稳定编译和执行。
因此，选择 `AssetHandle` 和 `SlotMap` 是因为：
- **低依赖**：它们属于引擎的核心资产管理逻辑（`Engine/src/Asset/` 下的纯头文件和轻量逻辑），基本只依赖 C++ 标准库及少量基础类定义。
- **纯逻辑**：它们没有对外部系统的副作用，不依赖窗口上下文、GPU 渲染链路或物理世界初始化，能够稳定编译和运行。
- **高价值**：作为资源系统的基石，`AssetHandle` 和 `SlotMap` 的可靠性直接影响上层游戏对象对各类资源的生命周期管理。

## 3. 如何配置并运行这些测试
由于主项目顶层 CMake 配置依赖了各类重型第三方库并且在当前纯代码沙盒环境中难以全面通过 `vcpkg` 安装满足（由于网络、环境隔离及包锁等问题导致 FFmpeg 缺失从而阻止了全局构建），这些单元测试采用了独立的子项目构建方式，仅提取纯逻辑的测试目标。

**执行步骤：**
```bash
# 进入测试目录
cd tests/

# 创建构建目录并进入
mkdir build && cd build

# 执行 CMake 配置 (引入轻量级的 GoogleTest)
cmake -G Ninja ..

# 构建测试目标
cmake --build .

# 运行所有的测试
ctest --output-on-failure
# 或者直接执行可执行文件
./EngineTests
```

## 4. 测试结果
我们在隔离的测试构建环境中成功编译并通过了所有的单元测试。未出现任何异常。
共 11 个测试用例：
- `AssetHandle`：包含 `DefaultConstructorIsInvalid`, `ValidHandle`, `Equality`, `Inequality`, `HashFunction`，共计 5 个。
- `SlotMap`：包含 `Initialization`, `InsertAndGet`, `Replace`, `RemoveAndGenerationInvalidation`, `GetInvalidHandle`, `Clear`，共计 6 个。

**测试执行输出：**
```text
100% tests passed, 0 tests failed out of 11
```

## 5. 当前未覆盖的部分及原因
- **跨模块复杂逻辑与子系统接口**：例如 `AssetManager`、`Scene` 以及相关的渲染系统等，由于它们直接依赖系统窗口、图形驱动、图形 API 以及第三方多媒体和物理引擎支持，在此次低依赖验证目标下未进行覆盖。
- **构建系统的深层集成**：由于系统依赖限制（缺少完整的 FFmpeg/OpenAL 运行环境等构建条件），未能将这些测试直接注册到根 CMake 目标中随项目主二进制共同编译，而是仅在 `tests/` 内构建测试。

## 6. 后续扩展测试建议
为了进一步提升代码覆盖率，如果后续需要扩展测试，建议按照以下模块依次切入：
1. **纯数学与基础数据结构类**：例如 `Core/UUID`，事件系统（Event 系统如果有的话）、以及一些单纯封装的数据载体。
2. **反射与序列化层**：如果 `Reflection/AutoSerializer` 或 `Scene/SceneSerializer` 可以支持纯内存或文件的模拟测试，不需要启动实际渲染，那么也非常值得测试。
3. **基于 Mock 的子系统测试**：对于依赖复杂外部模块的地方（如 `Renderer`），可以在未来考虑引入 GoogleMock 构建假接口（Mock Interfaces），阻断真实依赖再进行测试。