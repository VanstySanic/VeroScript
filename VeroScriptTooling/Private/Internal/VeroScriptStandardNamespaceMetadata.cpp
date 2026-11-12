// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptStandardNamespaceMetadata.h"

#include "VeroScriptLocale.h"
#include "VeroScriptLexer.h"

#include <array>

namespace VeroScript::Tooling::Detail
{
	namespace
	{
		constexpr std::array<FStandardNamespaceMetadata, 16> StandardNamespaces = {{
			{"std", "", "standard library root", "builtin",
				"VeroScript standard namespace root.",
				"Groups language builtins, standard types, and optional standard-library modules.",
				"VeroScript 标准命名空间根。",
				"用于组织语言内建、标准类型和可选标准库模块。",
				"chrono, console, io, filesystem, strings, concurrent"},
			{"std.compiler", "std", "compilation extension services", "stdlib/compiler.vs",
				"Compilation extension service entry point.",
				"Provides registration, context queries and generation output for explicitly enabled compilation extensions.",
				"脚本编译扩展服务入口。",
				"为明确启用的编译扩展提供注册、上下文查询和代码生成输出。",
				"registration, context, output, configuration, read_dependency, check_cancelled"},
			{"std.memory", "std", "managed bytes and runtime identity", "builtin",
				"Managed byte regions and runtime identity queries.",
				"Provides bounded buffers, views, leases, and explicit storage/object/data address queries.",
				"受管理字节区域与运行时身份查询。",
				"提供有边界的缓冲区、视图、租借及明确的存储槽、对象头和数据地址查询。",
				"buffer, view, readonly_view, object_id, address_of, read, write"},
			{"std.i18n", "std", "typed localization", "builtin",
				"Typed localization values and independent contexts.",
				"Provides immutable messages, named arguments, language configuration and host-backed resource snapshots.",
				"类型化本地化值与独立上下文。",
				"提供不可变消息、命名参数、语言配置和宿主资源快照。",
				"argument, arguments, configuration, text, version, message_template, resolved_text, snapshot, context, argument_kind, normalize_language_tag"},
			{"std.chrono", "std", "time", "builtin",
				"Time-related standard-library entry point.",
				"Provides clock and timestamp operations backed by the runtime.",
				"时间相关标准库入口。",
				"提供由运行时支持的时钟和时间戳操作。",
				"now, steady_now"},
			{"std.console", "std", "console", "builtin",
				"Console output and terminal capability entry point.",
				"Provides newline-free output, flushing, terminal capability queries, color and cursor control, "
				"window and line clearing, and visible window dimensions. Unsupported controls return false; "
				"plain text sinks receive no terminal control codes.",
				"控制台输出与终端能力入口。",
				"提供无换行输出、刷新、终端能力查询、颜色和光标控制、清屏清行及可见窗口尺寸。"
				"不支持的控制操作返回 false，普通文本输出不包含终端控制码。",
				"write, error, flush, clear, is_interactive, supports_color, supports_cursor, supports_clear, "
				"is_output_terminal, set_color, reset_style, clear_line, move_cursor, set_cursor_visible, width, height"},
			{"std.process", "std", "process control", "builtin",
				"Process-control standard-library entry point.",
				"Provides explicit process termination through the standard namespace.",
				"进程控制标准库入口。",
				"提供通过标准命名空间显式终止进程的功能。",
				"exit"},
			{"std.io", "std", "input and output", "stdlib/io.vs",
				"Input and output standard-library entry point.",
				"Provides console input and file-oriented I/O helpers.",
				"输入输出标准库入口。",
				"提供控制台输入和面向文件的 I/O 帮助函数。",
				"read_int, read_file, write_file"},
			{"std.filesystem", "std", "filesystem", "stdlib/io.vs",
				"Filesystem standard-library entry point.",
				"Provides path-independent file and directory operations.",
				"文件系统标准库入口。",
				"提供与路径表示无关的文件和目录操作。",
				"exists, is_file, is_directory, remove_dir, list_dir, mkdir"},
			{"std.strings", "std", "strings", "stdlib/string.vs",
				"String utility standard-library entry point.",
				"Provides search, slicing, splitting, joining, and normalization helpers.",
				"字符串工具标准库入口。",
				"提供查找、切片、拆分、连接和规范化帮助函数。",
				"contains, split, join"},
			{"std.concurrent", "std", "concurrency", "stdlib/concurrent.vs",
				"Concurrency standard-library entry point.",
				"Provides channels, tasks, synchronization primitives, and task orchestration.",
				"并发标准库入口。",
				"提供通道、任务、同步原语和任务编排功能。",
				"spawn_task, cancel_task, cancel_requested, make_channel, when_all"},
			{"std.algorithm", "std", "algorithms", "stdlib/algorithm.vs",
				"Generic algorithm standard-library entry point.",
				"Provides ordering, searching, copying, and range algorithms.",
				"通用算法标准库入口。",
				"提供排序、查找、复制和范围算法。",
				"sort, binary_search, shuffle"},
			{"std.numeric", "std", "numeric algorithms", "stdlib/numeric.vs",
				"Numeric algorithm standard-library entry point.",
				"Provides aggregate numeric operations for standard containers.",
				"数值算法标准库入口。",
				"为标准容器提供聚合数值操作。",
				"accumulate, sum, average"},
			{"std.random", "std", "random", "stdlib/random.vs",
				"Random-number standard-library entry point.",
				"Provides deterministic engines and typed random-number generation.",
				"随机数标准库入口。",
				"提供确定性随机引擎和带类型的随机数生成。",
				"make_engine, next_int, next_double"},
			{"std.environment", "std", "environment", "builtin",
				"Environment standard-library entry point.",
				"Provides process environment-variable access.",
				"环境标准库入口。",
				"提供进程环境变量访问功能。",
				"getenv, setenv"},
			{"std.this_thread", "std", "thread control", "builtin",
				"Current-thread standard-library entry point.",
				"Provides operations affecting the current executing thread.",
				"当前线程标准库入口。",
				"提供影响当前执行线程的操作。",
				"sleep"},
		}};

		std::string NormalizeNamespaceSpelling(const std::string_view Name)
		{
			std::string Normalized;
			Normalized.reserve(Name.size());
			for (std::size_t Index = 0; Index < Name.size(); ++Index)
			{
				if (Name[Index] == ':' && Index + 1 < Name.size() && Name[Index + 1] == ':')
				{
					Normalized.push_back('.');
					++Index;
				}
				else
				{
					Normalized.push_back(Name[Index]);
				}
			}
			return Normalized;
		}

		bool UsesSimplifiedChinese()
		{
			const std::string Locale = VeroScript::Locale::GetLanguageTag();
			return Locale == "zh-CN" || Locale.rfind("zh-Hans", 0) == 0;
		}
	}

	const FStandardNamespaceMetadata* FindStandardNamespaceMetadata(
		const std::string_view QualifiedName)
	{
		const std::string Normalized = NormalizeNamespaceSpelling(QualifiedName);
		for (const FStandardNamespaceMetadata& Metadata : StandardNamespaces)
		{
			if (Metadata.QualifiedName == Normalized)
			{
				return &Metadata;
			}
		}
		return nullptr;
	}

	std::string FormatNamespaceQualifiedName(const std::string_view QualifiedName)
	{
		std::string Result;
		Result.reserve(QualifiedName.size() + 4);
		for (const char Character : QualifiedName)
		{
			if (Character == '.')
			{
				Result += "::";
			}
			else
			{
				Result.push_back(Character);
			}
		}
		return Result;
	}

	std::string FormatStandardSourceSpelling(const std::string_view SourceText)
	{
		std::string ScopedText;
		char Quote = 0;
		for (std::size_t Index = 0; Index < SourceText.size(); ++Index)
		{
			const auto Character = SourceText[Index];
			if (Quote)
			{
				ScopedText += Character;
				if (Character == '\\' && Index + 1 < SourceText.size())
				{
					ScopedText += SourceText[++Index];
				}
				else if (Character == Quote)
				{
					Quote = 0;
				}
				continue;
			}
			if (Character == '\'' || Character == '"')
			{
				Quote = Character;
			}
			const bool Local = SourceText.substr(Index, 6) == "$local";
			if (Local || SourceText.substr(Index, 6) == "$arity")
			{
				auto End = Index + 6;
				while (End < SourceText.size() && SourceText[End] >= '0' && SourceText[End] <= '9')
				{
					++End;
				}
				if (End > Index + 6 && End < SourceText.size() && SourceText[End] == '.')
				{
					Index = Local ? End : End - 1;
					continue;
				}
			}
			ScopedText += Character;
		}
		const std::string_view Text = ScopedText;
		if (Text.find("std") == std::string_view::npos || Text.find('.') == std::string_view::npos)
		{
			return std::string(Text);
		}
		VeroScriptLexer Lexer(Text);
		const auto& Tokens = Lexer.Tokenize();
		std::string Result;
		std::size_t Start = 0;
		for (std::size_t Index = 0; Index < Tokens.size(); ++Index)
		{
			if (Tokens[Index].Kind != ETokenKind::Identifier || Tokens[Index].Lexeme != "std")
			{
				continue;
			}
			while (Index + 2 < Tokens.size()
				&& Tokens[Index + 1].Kind == ETokenKind::Symbol
				&& (Tokens[Index + 1].Lexeme == "." || Tokens[Index + 1].Lexeme == "::")
				&& (Tokens[Index + 2].Kind == ETokenKind::Identifier
					|| Tokens[Index + 2].Kind == ETokenKind::Keyword))
			{
				if (Tokens[Index + 1].Lexeme == ".")
				{
					const std::size_t Offset = static_cast<std::size_t>(Tokens[Index + 1].Lexeme.data() - Text.data());
					Result.append(Text.substr(Start, Offset - Start));
					Result += "::";
					Start = Offset + 1;
				}
				Index += 2;
			}
		}
		Result.append(Text.substr(Start));
		return Result;
	}

	std::string FormatStandardNamespaceDocumentation(const FStandardNamespaceMetadata& Metadata)
	{
		const bool bChinese = UsesSimplifiedChinese();
		const std::string_view Brief = bChinese && !Metadata.BriefZhCN.empty()
			? Metadata.BriefZhCN
			: Metadata.BriefEnglish;
		const std::string_view Detail = bChinese && !Metadata.DetailZhCN.empty()
			? Metadata.DetailZhCN
			: Metadata.DetailEnglish;
		std::string Result(Brief);
		if (!Detail.empty())
		{
			Result += "\n\n";
			Result += Detail;
		}
		if (!Metadata.Category.empty())
		{
			Result += bChinese ? "\n\n类别：" : "\n\nCategory: ";
			Result += Metadata.Category;
		}
		Result += bChinese ? "\n\n模块：" : "\n\nModule: ";
		Result += Metadata.Module;
		if (!Metadata.RepresentativeMembers.empty())
		{
			Result += bChinese ? "\n代表性成员：" : "\nRepresentative members: ";
			Result += Metadata.RepresentativeMembers;
		}
		return Result;
	}

	std::string FormatStandardNamespaceDetail(const FStandardNamespaceMetadata& Metadata)
	{
		std::string Result = "namespace ";
		Result += FormatNamespaceQualifiedName(Metadata.QualifiedName);
		Result += " (";
		Result += Metadata.Module;
		Result += ')';
		return Result;
	}
}
