#include "Core/CrashHandler.h"
#include "Asset/PathUtils.h"
#include "Core/Log.h"
#include "engpch.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <DbgHelp.h>
#include <Windows.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

namespace Engine
{
    namespace CrashHandler
    {
#ifdef _WIN32
        namespace
        {
            void ReportDumpPath(const std::filesystem::path& dumpPath)
            {
                if (Log::GetCoreLogger())
                    ENGINE_CORE_ERROR("[CrashDump] Wrote minidump to '{0}'", dumpPath.string());
                else
                    std::fprintf(stderr, "[CrashDump] Wrote minidump to '%s'\n", dumpPath.string().c_str());
            }

            LONG WINAPI HandleUnhandledException(EXCEPTION_POINTERS* exceptionPointers)
            {
                std::error_code ec;
                const auto dumpDirectory = PathUtils::GetLogsRoot() / "crash";
                std::filesystem::create_directories(dumpDirectory, ec);

                SYSTEMTIME now{};
                GetLocalTime(&now);

                char dumpFileName[128];
                std::snprintf(dumpFileName, sizeof(dumpFileName), "crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp", now.wYear,
                              now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());

                const auto dumpPath = dumpDirectory / dumpFileName;
                HANDLE dumpFile = CreateFileA(dumpPath.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
                if (dumpFile == INVALID_HANDLE_VALUE)
                    return EXCEPTION_EXECUTE_HANDLER;

                MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
                dumpInfo.ThreadId = GetCurrentThreadId();
                dumpInfo.ExceptionPointers = exceptionPointers;
                dumpInfo.ClientPointers = FALSE;

                const BOOL dumpResult =
                    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                                      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData),
                                      exceptionPointers ? &dumpInfo : nullptr, nullptr, nullptr);

                CloseHandle(dumpFile);

                if (dumpResult == TRUE)
                    ReportDumpPath(dumpPath);
                else if (Log::GetCoreLogger())
                    ENGINE_CORE_ERROR("[CrashDump] Failed to write dump (GetLastError={0})", GetLastError());

                return EXCEPTION_EXECUTE_HANDLER;
            }

            void TerminateWithDump()
            {
                RaiseException(0xE0000001u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
                std::abort();
            }
        } // namespace
#endif

        void Install()
        {
#ifdef _WIN32
            SetUnhandledExceptionFilter(HandleUnhandledException);
            std::set_terminate(TerminateWithDump);
            ENGINE_CORE_INFO("[CrashDump] Unhandled exception filter installed");
#else
            ENGINE_CORE_INFO("[CrashDump] Minidump support is only available on Windows");
#endif
        }
    } // namespace CrashHandler
} // namespace Engine
