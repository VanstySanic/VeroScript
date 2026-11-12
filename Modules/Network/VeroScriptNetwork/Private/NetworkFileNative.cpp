// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "NetworkFileInternal.h"
#include <algorithm>
#include <cerrno>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
static_assert(sizeof(off_t) >= 8, "VeroScript file streams require 64-bit native file offsets");
#endif

namespace VeroScript::Network::Detail
{
	namespace
	{
		std::atomic<std::uint64_t> NextTemporary{1};
		int LastError() noexcept
		{
			#if defined(_WIN32)
			return static_cast<int>(GetLastError());
			#else
			return errno;
			#endif
		}
		#if !defined(_WIN32)
		bool CommitNoReplace(const std::filesystem::path& Source, const std::filesystem::path& Destination,
		    bool& Linked) noexcept
		{
			#if defined(SYS_renameat2)
			// Android permits rename but can deny hard links through SELinux.
			if (::syscall(SYS_renameat2, AT_FDCWD, Source.c_str(), AT_FDCWD, Destination.c_str(), RENAME_NOREPLACE) == 0)
			{
				return true;
			}
			if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP)
			{
				return false;
			}
			#endif
			Linked = ::link(Source.c_str(), Destination.c_str()) == 0;
			return Linked;
		}
		#endif
		void Error(FFileJob& J, EError Code, int System = 0) noexcept
		{
			if (J.NativeError == EError::None)
			{
				J.NativeError = Code;
				J.SystemCode = System;
			}
		}
		bool Close(std::intptr_t& File, FFileJob& J, bool Required = false) noexcept
		{
			if (File == -1)
			{
				return true;
			}
			const auto Value = File;
			File = -1;
			#if defined(_WIN32)
			const bool Ok = CloseHandle(reinterpret_cast<HANDLE>(Value)) != 0;
			#else
			// A failed close must not be retried: the descriptor may already have been reused.
			const bool Ok = ::close(static_cast<int>(Value)) == 0;
			#endif
			if (!Ok)
			{
				const auto System = LastError();
				J.Progress->CleanupSystemCode = System;
				if (Required)
				{
					Error(J, EError::Write, System);
				}
			}
			return Ok;
		}
		std::intptr_t OpenRead(const std::filesystem::path& Path, std::uint64_t& Size, FFileJob& J)
		{
			#if defined(_WIN32)
			auto Handle = CreateFileW(Path.c_str(),
			    GENERIC_READ,
			    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			    nullptr,
			    OPEN_EXISTING,
			    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
			    nullptr);
			if (Handle == INVALID_HANDLE_VALUE)
			{
				Error(J, EError::Read, LastError());
				return -1;
			}
			std::intptr_t File = reinterpret_cast<std::intptr_t>(Handle);
			LARGE_INTEGER Length;
			if (GetFileType(Handle) != FILE_TYPE_DISK || !GetFileSizeEx(Handle, &Length) || Length.QuadPart < 0)
			{
				Error(J, EError::Read, LastError());
				Close(File, J);
				return -1;
			}
			Size = static_cast<std::uint64_t>(Length.QuadPart);
			#else
			const int Handle = ::open(Path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (Handle < 0)
			{
				Error(J, EError::Read, LastError());
				return -1;
			}
			std::intptr_t File = Handle;
			struct stat Info{};
			if (::fstat(Handle, &Info) != 0 || !S_ISREG(Info.st_mode) || Info.st_size < 0)
			{
				Error(J, EError::Read, errno ? errno : EINVAL);
				Close(File, J);
				return -1;
			}
			Size = static_cast<std::uint64_t>(Info.st_size);
			#endif
			return File;
		}
		bool Seek(std::intptr_t File, std::uint64_t Offset, FFileJob& J)
		{
			if (Offset > static_cast<std::uint64_t>(INT64_MAX))
			{
				Error(J, EError::InvalidArgument);
				return false;
			}
			#if defined(_WIN32)
			LARGE_INTEGER Position;
			Position.QuadPart = static_cast<LONGLONG>(Offset);
			const bool Ok = SetFilePointerEx(reinterpret_cast<HANDLE>(File), Position, nullptr, FILE_BEGIN) != 0;
			#else
			const bool Ok = ::lseek(static_cast<int>(File), static_cast<off_t>(Offset), SEEK_SET) >= 0;
			#endif
			if (!Ok)
			{
				Error(J, EError::Read, LastError());
			}
			return Ok;
		}
		bool Read(std::intptr_t File, FFileJob& J, std::size_t Count)
		{
			J.Bytes = 0;
			while (J.Bytes < Count && !J.Cancelled)
			{
				#if defined(_WIN32)
				DWORD Bytes = 0;
				const bool Ok = ReadFile(reinterpret_cast<HANDLE>(File),
				                    J.Buffer.data() + J.Bytes,
				                    static_cast<DWORD>(Count - J.Bytes),
				                    &Bytes,
				                    nullptr) != 0;
				if (!Ok)
				#else
				const auto Bytes = ::read(static_cast<int>(File), J.Buffer.data() + J.Bytes, Count - J.Bytes);
				if (Bytes < 0 && errno == EINTR)
				{
					continue;
				}
				if (Bytes < 0)
				#endif
				{
					Error(J, EError::Read, LastError());
					return false;
				}
				if (!Bytes)
				{
					Error(J, EError::Read); // The selected snapshot/range became shorter.
					return false;
				}
				J.Bytes += static_cast<std::size_t>(Bytes);
			}
			return !J.Cancelled;
		}
		bool Write(FFileJob& J)
		{
			std::size_t Offset = 0;
			while (Offset < J.Bytes && !J.Cancelled)
			{
				#if defined(_WIN32)
				DWORD Bytes = 0;
				const bool Ok = WriteFile(reinterpret_cast<HANDLE>(J.File),
				                    J.Buffer.data() + Offset,
				                    static_cast<DWORD>(J.Bytes - Offset),
				                    &Bytes,
				                    nullptr) != 0;
				if (!Ok)
				#else
				const auto Bytes = ::write(static_cast<int>(J.File), J.Buffer.data() + Offset, J.Bytes - Offset);
				if (Bytes < 0 && errno == EINTR)
				{
					continue;
				}
				if (Bytes < 0)
				#endif
				{
					Error(J, EError::Write, LastError());
					return false;
				}
				if (!Bytes)
				{
					Error(J, EError::Write);
					return false;
				}
				Offset += static_cast<std::size_t>(Bytes);
				J.Progress->Bytes.fetch_add(static_cast<std::uint64_t>(Bytes));
			}
			return !J.Cancelled;
		}
		void RemoveTemporary(FFileJob& J) noexcept
		{
			if (J.Temporary.empty() || J.WriteOptions.bKeepPartial || J.Progress->Commit == EFileCommitState::Committed)
			{
				return;
			}
			#if defined(_WIN32)
			const bool Ok = DeleteFileW(J.Temporary.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
			#else
			const bool Ok = ::unlink(J.Temporary.c_str()) == 0 || errno == ENOENT;
			#endif
			if (Ok)
			{
				std::lock_guard<std::mutex> Lock(J.Progress->Mutex);
				J.Progress->PartialPath.clear();
			}
			else
			{
				J.Progress->CleanupSystemCode = LastError();
			}
		}
		void Abort(FFileJob& J) noexcept
		{
			Close(J.Prefix, J);
			Close(J.File, J);
			if (J.Progress->Commit != EFileCommitState::Committed)
			{
				J.Progress->Commit = EFileCommitState::Failed;
				if (J.bWrite)
				{
					RemoveTemporary(J);
				}
			}
		}
		void Open(FFileJob& J)
		{
			if (!J.bWrite)
			{
				std::uint64_t Size = 0;
				J.File = OpenRead(J.Path, Size, J);
				if (J.File == -1)
				{
					return;
				}
				const auto& O = J.ReadOptions;
				if (O.Offset > Size || (O.Length && *O.Length > Size - O.Offset))
				{
					Error(J, EError::InvalidArgument);
					return;
				}
				J.Remaining = O.Length.value_or(Size - O.Offset);
				if (J.Remaining > O.MaxBytes)
				{
					Error(J, EError::Limit);
				}
				else if (Seek(J.File, O.Offset, J))
				{
					J.Progress->Total = J.Remaining;
					J.Progress->bHasTotal = true;
				}
				return;
			}
			const auto& O = J.WriteOptions;
			if (!J.PrefixPath.empty())
			{
				std::uint64_t Size = 0;
				J.Prefix = OpenRead(J.PrefixPath, Size, J);
				if (J.Prefix == -1)
				{
					return;
				}
				if (Size != O.PrefixLength)
				{
					Error(J, EError::State);
					return;
				}
				J.PrefixRemaining = Size;
			}
			for (unsigned Attempt = 0; Attempt < 64; ++Attempt)
			{
				auto Temporary = J.Path;
				#if defined(_WIN32)
				const auto Process = GetCurrentProcessId();
				#else
				const auto Process = ::getpid();
				#endif
				Temporary += ".veroscript-part-" + std::to_string(Process) + "-" + std::to_string(NextTemporary++);
				// Prepare all owning path storage before creation; commit only nonallocating moves afterward.
				auto Text = FilePathUtf8(Temporary);
				#if defined(_WIN32)
				auto File = CreateFileW(Temporary.c_str(),
				    GENERIC_WRITE,
				    FILE_SHARE_READ,
				    nullptr,
				    CREATE_NEW,
				    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
				    nullptr);
				if (File != INVALID_HANDLE_VALUE)
				{
					J.Temporary = std::move(Temporary);
					J.File = reinterpret_cast<std::intptr_t>(File);
					std::lock_guard<std::mutex> Lock(J.Progress->Mutex);
					J.Progress->PartialPath = std::move(Text);
					return;
				}
				const auto Code = LastError();
				if (Code == ERROR_FILE_EXISTS || Code == ERROR_ALREADY_EXISTS)
				{
					continue;
				}
				#else
				const int File = ::open(Temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
				if (File >= 0)
				{
					J.Temporary = std::move(Temporary);
					J.File = File;
					std::lock_guard<std::mutex> Lock(J.Progress->Mutex);
					J.Progress->PartialPath = std::move(Text);
					return;
				}
				const auto Code = LastError();
				if (Code == EEXIST)
				{
					continue;
				}
				#endif
				Error(J, EError::Write, Code);
				return;
			}
			Error(J, EError::Limit);
		}
		void Finish(FFileJob& J)
		{
			Close(J.Prefix, J);
			if (!J.bWrite)
			{
				Close(J.File, J);
				return;
			}
			if (J.WriteOptions.ExpectedLength && J.Progress->Bytes != *J.WriteOptions.ExpectedLength)
			{
				Error(J, EError::Protocol);
				return;
			}
			if (J.WriteOptions.bFlushToDisk)
			{
				#if defined(_WIN32)
				const bool Ok = FlushFileBuffers(reinterpret_cast<HANDLE>(J.File)) != 0;
				#else
				const bool Ok = ::fsync(static_cast<int>(J.File)) == 0;
				#endif
				if (!Ok)
				{
					Error(J, EError::Write, LastError());
					return;
				}
			}
			if (!Close(J.File, J, true) || J.Cancelled)
			{
				return;
			}
			J.Progress->Commit = EFileCommitState::Committing;
			#if defined(_WIN32)
			const bool Ok = MoveFileExW(J.Temporary.c_str(),
			                    J.Path.c_str(),
			                    J.WriteOptions.bReplaceExisting ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
			#else
			bool Linked = false;
			const bool Ok = J.WriteOptions.bReplaceExisting ? ::rename(J.Temporary.c_str(), J.Path.c_str()) == 0
			                                                : CommitNoReplace(J.Temporary, J.Path, Linked);
			#endif
			if (!Ok)
			{
				Error(J, EError::Write, LastError());
				return;
			}
			J.Progress->Commit = EFileCommitState::Committed;
			#if !defined(_WIN32)
			if (Linked && ::unlink(J.Temporary.c_str()) != 0)
			{
				J.Progress->CleanupSystemCode = LastError();
				return;
			}
			#endif
			std::lock_guard<std::mutex> Lock(J.Progress->Mutex);
			J.Progress->PartialPath.clear();
		}
	} // namespace
	void ExecuteFileCommand(FFileJob& J) noexcept
	{
		try
		{
			if (J.Cancelled || J.Command == EFileCommand::Abort)
			{
				Abort(J);
				return;
			}
			switch (J.Command)
			{
			case EFileCommand::Open:
				{
					Open(J);
					break;
				}
			case EFileCommand::Read:
				{
					if (Read(J.File, J, static_cast<std::size_t>(std::min<std::uint64_t>(J.Remaining, J.Buffer.size()))))
					{
						J.Remaining -= J.Bytes;
						J.Progress->Bytes.fetch_add(J.Bytes);
					}
					break;
				}
			case EFileCommand::Prefix:
				{
					if (Read(J.Prefix,
					        J,
					        static_cast<std::size_t>(std::min<std::uint64_t>(J.PrefixRemaining, J.Buffer.size()))))
					{
						if (Write(J))
						{
							J.PrefixRemaining -= J.Bytes;
						}
					}
					break;
				}
			case EFileCommand::Write:
				{
					Write(J);
					break;
				}
			case EFileCommand::Finish:
				{
					Finish(J);
					break;
				}
			case EFileCommand::Abort:
				{
					break;
				}

			default:
				{
					break;
				}
			}
		}
		catch (const std::bad_alloc&)
		{
			Error(J, EError::Allocation);
		}
		catch (const std::length_error&)
		{
			Error(J, EError::Limit);
		}
		catch (...)
		{
			Error(J, EError::Internal);
		}
		if (J.NativeError != EError::None || J.Cancelled)
		{
			Abort(J);
		}
	}
} // namespace VeroScript::Network::Detail
