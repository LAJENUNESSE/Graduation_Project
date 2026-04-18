#include <gtest/gtest.h>

#include "Core/Log.h"

// 自定义测试入口：在跑 GoogleTest 之前初始化 Engine 日志子系统。
//
// 背景：Engine 中部分代码（例如 SceneEntityIndex::Insert 对重复 UUID 的处理，
// WorldTransformService 对层级深度超限的警告）会调用 ENGINE_CORE_* 日志宏，
// 这些宏展开为 Engine::Log::GetCoreLogger()->xxx(...)。若测试可执行文件直接使用
// gtest_main 自带的 main，则 Engine::Log::Init() 永远不会被调用，s_CoreLogger
// 保持为空 shared_ptr，任何日志调用都会解引用空指针 -> SEGFAULT。
// ASAN/UBSAN 构建下，这会让相关测试（如 SceneEntityIndex.OverwriteExisting）
// 非零退出，进而导致 CI 的 Linux (GCC + ASAN/UBSAN) job 失败。
//
// 解决方案：提供自定义 main，在 GoogleTest 初始化之前调用 Log::Init()，使所有
// 测试都能安全触发 Engine 日志路径。
int main(int argc, char** argv)
{
    Engine::Log::Init();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
