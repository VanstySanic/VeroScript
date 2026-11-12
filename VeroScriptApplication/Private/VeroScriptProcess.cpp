// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProcess.h"
#include "ApplicationInternal.h"
#include <chrono>
#include <thread>
#include <array>
#include <algorithm>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
extern char** environ;
#endif

namespace VeroScript::Application
{
	bool RunProcess(const FProcessRequest& R, int& ExitCode, FDiagnostic& Error)
	{
		using namespace Detail;
		ExitCode = -1;
		#if defined(_WIN32)
		HANDLE Process = nullptr, Thread = nullptr, Job = nullptr;
		HANDLE ReadOut = nullptr, WriteOut = nullptr, ReadError = nullptr, WriteError = nullptr, Input = nullptr;
		std::vector<std::uint8_t> AttributeStorage;
		LPPROC_THREAD_ATTRIBUTE_LIST Attributes = nullptr;
		#else
		pid_t Process = -1;
		int OutputPipe[2]{-1, -1}, ErrorPipe[2]{-1, -1};
		#endif
		const auto Stop = [&]()
		{
			#if defined(_WIN32)
			if (Process)
			{
				if (Job)
				{
					TerminateJobObject(Job, 125);
				}
				else
				{
					TerminateProcess(Process, 125);
				}
				WaitForSingleObject(Process, INFINITE);
			}
			#else
			if (Process > 0)
			{
				::kill(-Process, SIGKILL);
				::kill(Process, SIGKILL);
				int Status;
				while (::waitpid(Process, &Status, 0) < 0 && errno == EINTR)
				{
				}
				Process = -1;
			}
			#endif
		};
		const auto Close = [&]()
		{
			#if defined(_WIN32)
			if (Thread)
			{
				CloseHandle(Thread);
			}
			if (Process)
			{
				CloseHandle(Process);
			}
			if (Job)
			{
				CloseHandle(Job);
			}
			for (auto* H : {&ReadOut, &WriteOut, &ReadError, &WriteError, &Input})
			{
				if (*H)
				{
					CloseHandle(*H);
					*H = nullptr;
				}
			}
			if (Attributes)
			{
				DeleteProcThreadAttributeList(Attributes);
				Attributes = nullptr;
			}
			Thread = Process = Job = nullptr;
			#else
			for (auto* Fd : {&OutputPipe[0], &OutputPipe[1], &ErrorPipe[0], &ErrorPipe[1]})
			{
				if (*Fd >= 0)
				{
					::close(*Fd);
					*Fd = -1;
				}
			}
			#endif
		};
		try
		{
			Physical(PathUtil::FromUtf8(R.Executable));
			Physical(PathUtil::FromUtf8(R.WorkingDirectory));
			if (R.InheritEnvironment && !R.Environment.empty())
			{
				Fail("process-environment", "inherited and explicit environments cannot be combined");
			}
			for (const auto& A : R.Arguments)
			{
				if (A.find('\0') != std::string::npos)
				{
					Fail("process-argument", "process arguments cannot contain NUL");
				}
			}
			for (const auto& E : R.Environment)
			{
				if (E.first.empty() || E.first.find_first_of("=\0", 0, 2) != std::string::npos || E.second.find('\0') != std::string::npos)
				{
					Fail("process-environment", "invalid environment variable");
				}
			}
			if (R.CheckCancelled)
			{
				R.CheckCancelled();
			}
			#if defined(_WIN32)
			const auto Wide = [](const std::string& S)
			{
				if (S.empty()) return std::wstring{};
				const int Size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, S.data(), static_cast<int>(S.size()), nullptr, 0);
				if (Size <= 0)
				{
					Fail("process-utf8", "invalid UTF-8 in process request");
				}
				std::wstring W(Size, L'\0');
				MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, S.data(), static_cast<int>(S.size()), W.data(), Size);
				return W;
			};
			const auto Quote = [&](const std::string& S)
			{
				const auto W = Wide(S);
				std::wstring Out = L"\"";
				std::size_t Slashes = 0;
				for (const auto C : W)
				{
					if (C == L'\\')
					{
						++Slashes;
						continue;
					}
					Out.append(Slashes * (C == L'"' ? 2 : 1), L'\\');
					if (C == L'"')
					{
						Out += L'\\';
					}
					Out += C;
					Slashes = 0;
				}
				Out.append(Slashes * 2, L'\\');
				Out += L'"';
				return Out;
			};
			std::wstring Command = Quote(R.Executable);
			for (const auto& A : R.Arguments)
			{
				Command += L' ';
				Command += Quote(A);
			}
			if (Command.size() > 32766)
			{
				Fail("process-arguments", "process command line is too long");
			}
			std::wstring Env;
			for (const auto& E : R.Environment)
			{
				Env += Wide(E.first + "=" + E.second);
				Env += L'\0';
			}
			Env += L'\0';
			if (Env.size() == 1)
			{
				Env += L'\0';
			}
			SECURITY_ATTRIBUTES Security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
			if (!CreatePipe(&ReadOut, &WriteOut, &Security, 0) || !CreatePipe(&ReadError, &WriteError, &Security, 0) ||
			    !SetHandleInformation(ReadOut, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(ReadError, HANDLE_FLAG_INHERIT, 0))
			{
				Fail("process-pipe", "cannot create bounded process output pipes");
			}
			const auto ParentInput = GetStdHandle(STD_INPUT_HANDLE);
			if (!ParentInput || ParentInput == INVALID_HANDLE_VALUE)
			{
				Input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Security, OPEN_EXISTING, 0, nullptr);
			}
			else if (!DuplicateHandle(GetCurrentProcess(), ParentInput, GetCurrentProcess(), &Input, 0, TRUE, DUPLICATE_SAME_ACCESS))
			{
				Fail("process-input", "cannot inherit process input");
			}
			if (!Input || Input == INVALID_HANDLE_VALUE)
			{
				Input = nullptr;
				Fail("process-input", "cannot open process input");
			}
			STARTUPINFOEXW Start{};
			Start.StartupInfo.cb = sizeof(Start);
			Start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
			Start.StartupInfo.hStdInput = Input;
			Start.StartupInfo.hStdOutput = WriteOut;
			Start.StartupInfo.hStdError = WriteError;
			SIZE_T AttributeSize = 0;
			InitializeProcThreadAttributeList(nullptr, 1, 0, &AttributeSize);
			AttributeStorage.resize(AttributeSize);
			Attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(AttributeStorage.data());
			if (!InitializeProcThreadAttributeList(Attributes, 1, 0, &AttributeSize))
			{
				Attributes = nullptr;
				Fail("process-handles", "cannot initialize process handle list");
			}
			const HANDLE Inherited[]{Input, WriteOut, WriteError};
			if (!UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, const_cast<HANDLE*>(Inherited), sizeof(Inherited), nullptr, nullptr))
			{
				Fail("process-handles", "cannot restrict inherited handles");
			}
			Start.lpAttributeList = Attributes;
			PROCESS_INFORMATION Info{};
			Job = CreateJobObjectW(nullptr, nullptr);
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
			Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			if (!Job || !SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
			{
				Fail("process-job", "cannot create an owned process job");
			}
			if (!CreateProcessW(PathUtil::FromUtf8(R.Executable).c_str(), Command.data(), nullptr, nullptr, TRUE,
			        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, R.InheritEnvironment ? nullptr : Env.data(),
			        PathUtil::FromUtf8(R.WorkingDirectory).c_str(), &Start.StartupInfo, &Info))
			{
				Fail("process-create", "cannot start process", R.Executable);
			}
			Process = Info.hProcess;
			Thread = Info.hThread;
			CloseHandle(WriteOut);
			WriteOut = nullptr;
			CloseHandle(WriteError);
			WriteError = nullptr;
			CloseHandle(Input);
			Input = nullptr;
			if (!AssignProcessToJobObject(Job, Process) || ResumeThread(Thread) == static_cast<DWORD>(-1))
			{
				Fail("process-job", "cannot attach child process to its job");
			}
			#else
			std::vector<std::string> ArgStorage{R.Executable}, EnvStorage;
			ArgStorage.insert(ArgStorage.end(), R.Arguments.begin(), R.Arguments.end());
			std::vector<char*> Args, Environment;
			for (auto& A : ArgStorage)
			{
				Args.push_back(A.data());
			}
			Args.push_back(nullptr);
			for (const auto& E : R.Environment)
			{
				EnvStorage.push_back(E.first + "=" + E.second);
			}
			for (auto& E : EnvStorage)
			{
				Environment.push_back(E.data());
			}
			Environment.push_back(nullptr);
			if (::pipe(OutputPipe) != 0 || ::pipe(ErrorPipe) != 0)
			{
				Fail("process-pipe", "cannot create process output pipes");
			}
			for (auto* Fd : {&OutputPipe[0], &OutputPipe[1], &ErrorPipe[0], &ErrorPipe[1]})
			{
				if (*Fd < 3)
				{
					const int Copy = ::fcntl(*Fd, F_DUPFD, 3);
					::close(*Fd);
					*Fd = Copy;
				}
				if (*Fd < 0 || ::fcntl(*Fd, F_SETFD, FD_CLOEXEC) != 0)
				{
					Fail("process-pipe", "cannot configure process output pipes");
				}
			}
			if (::fcntl(OutputPipe[0], F_SETFL, O_NONBLOCK) != 0 || ::fcntl(ErrorPipe[0], F_SETFL, O_NONBLOCK) != 0)
			{
				Fail("process-pipe", "cannot configure nonblocking output");
			}
			Process = ::fork();
			if (Process < 0)
			{
				Fail("process-fork", "cannot start child process");
			}
			if (Process == 0)
			{
				::setpgid(0, 0);
				if (::dup2(OutputPipe[1], STDOUT_FILENO) < 0 || ::dup2(ErrorPipe[1], STDERR_FILENO) < 0)
				{
					_exit(126);
				}
				for (const int Fd : {OutputPipe[0], OutputPipe[1], ErrorPipe[0], ErrorPipe[1]})
				{
					::close(Fd);
				}
				if (::chdir(R.WorkingDirectory.c_str()) != 0)
				{
					_exit(126);
				}
				::execve(R.Executable.c_str(), Args.data(), R.InheritEnvironment ? environ : Environment.data());
				_exit(127);
			}
			::setpgid(Process, Process);
			::close(OutputPipe[1]);
			OutputPipe[1] = -1;
			::close(ErrorPipe[1]);
			ErrorPipe[1] = -1;
			#endif
			std::uint64_t OutputBytes = 0;
			const auto Emit = [&](const char* Buffer, std::size_t Bytes, bool StandardError)
			{
				if (R.MaxOutputBytes && Bytes > R.MaxOutputBytes - OutputBytes)
				{
					Fail("process-output", "child process exceeded its output budget");
				}
				OutputBytes += Bytes;
				if (R.Output)
				{
					R.Output(std::string_view(Buffer, Bytes), StandardError);
				}
			};
			const auto Drain = [&]()
			{
				std::array<char, 65536> Buffer{};
				for (unsigned Stream = 0; Stream < 2; ++Stream)
				{
					for (unsigned Chunk = 0; Chunk < 16; ++Chunk)
					{
						if (R.CheckCancelled)
						{
							R.CheckCancelled();
						}
						#if defined(_WIN32)
						const HANDLE H = Stream ? ReadError : ReadOut;
						DWORD Available = 0, Read = 0;
						if (!PeekNamedPipe(H, nullptr, 0, nullptr, &Available, nullptr) || !Available)
						{
							break;
						}
						if (!::ReadFile(H, Buffer.data(), static_cast<DWORD>(std::min<std::size_t>(Buffer.size(), Available)), &Read, nullptr) || !Read)
						{
							break;
						}
						Emit(Buffer.data(), Read, Stream != 0);
						#else
						const auto Read = ::read(Stream ? ErrorPipe[0] : OutputPipe[0], Buffer.data(), Buffer.size());
						if (Read < 0 && errno == EINTR)
						{
							continue;
						}
						if (Read < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
						{
							Fail("process-pipe", "cannot drain process output");
						}
						if (Read <= 0)
						{
							break;
						}
						Emit(Buffer.data(), static_cast<std::size_t>(Read), Stream != 0);
						#endif
					}
				}
			};
			const auto Started = std::chrono::steady_clock::now();
			for (;;)
			{
				Drain();
				if (R.CheckCancelled)
				{
					R.CheckCancelled();
				}
				if (R.TimeoutMilliseconds && std::chrono::steady_clock::now() - Started > std::chrono::milliseconds(R.TimeoutMilliseconds))
				{
					Fail("process-timeout", "child process exceeded its deadline");
				}
				#if defined(_WIN32)
				const auto Wait = WaitForSingleObject(Process, 10);
				if (Wait == WAIT_OBJECT_0)
				{
					DWORD Code;
					if (!GetExitCodeProcess(Process, &Code))
					{
						Fail("process-exit", "cannot obtain process exit code");
					}
					ExitCode = static_cast<int>(Code);
					break;
				}
				if (Wait == WAIT_FAILED)
				{
					Fail("process-wait", "cannot wait for child process");
				}
				#else
				int Status;
				const auto Wait = ::waitpid(Process, &Status, WNOHANG);
				if (Wait == Process)
				{
					::kill(-Process, SIGKILL);
					ExitCode = WIFEXITED(Status) ? WEXITSTATUS(Status) : 128 + WTERMSIG(Status);
					Process = -1;
					break;
				}
				if (Wait < 0 && errno != EINTR)
				{
					Fail("process-wait", "cannot wait for child process");
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				#endif
			}
			#if defined(_WIN32)
			TerminateJobObject(Job, 125);
			#endif
			Drain();
			Close();
			Error = {};
			return true;
		}
		catch (const FFailure& E)
		{
			Stop();
			Close();
			Error = E.Diagnostic;
			return false;
		}
		catch (const std::exception& E)
		{
			Stop();
			Close();
			Error = {"process", E.what(), R.Executable, {}};
			return false;
		}
	}
}
