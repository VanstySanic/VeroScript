// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

//
// VeroScriptTooling — IDE / analysis surfaces over the VeroScript language module.
// Public facade: Analyzer, Workspace, Outline, Navigation, Hover, Complete, CallHelp,
// FindReferences, DocumentHighlight, WorkspaceSymbols, Rename, Format, DebugSession,
// DapAdapter / DapStdio, LspStdio.
// Depends on VeroScript (language + Compile). Low-level AST queries live in
// Private/Internal (Detail) and may include Private headers.
//
// Preprocessor notes for IDE hosts:
// - Analyze / query APIs honor Defines (`-D`) via ModuleLoader preprocess.
// - FindReferences / Highlight / Rename drop hits in inactive `#if` bodies.
// - Object- and function-like `#define` names resolve for Hover / Navigation when no
//   AST symbol matches; Complete also offers active macros (InsertText adds `(` for
//   function-like) and preprocessor completions after `#`: directive names (`define`, `include`,
//   `ifdef`, …), macro names after `#define`/`#ifdef`/`#ifndef`/`#undef`, and `defined(` /
//   macros inside `#if`/`#elif`. CLI `-D` remains object-like only (`NAME` / `NAME=value`).
// - `#include "module.vs"` is modeled as re-exporting `export import`: path completion,
//   Hover, Navigation, Workspace analysis, references, and semantic classification follow
//   the transitive exported closure while keeping target-private declarations hidden.
// - Preprocess byte map (`OutByteToPhysical`) maps physical caret ↔ preprocessed AST lines
//   for SymbolQuery, Complete, SignatureHelp, FindReferences, Outline, Analyzer diagnostics,
//   WorkspaceSymbols (via Outline), Analyzer diagnostics (incl. imported files), and debug
//   breakpoints/stack (line + column).
// - Format is lexical only (does not expand/reformat macro bodies). Function-like macro
//   *formatting* is unsupported; expansion itself is handled by the preprocessor.
// - Soft builtins cover pair/shared/handle/containers (incl. deque.values), delegate/multicast_delegate
//   type + members (delegate.vs), concurrent.vs free wrappers, the print/printf/format/formatf
//   family (preferred over string.vs array wrappers on Complete/CallHelp/Hover),
//   auto foreach / auto& [k,v] binding
//   inference, operator mirrors (!=←==, >↔<) for Hover/Goto/Find Refs, concurrent overload
//   disambiguation in SignatureHelp (channel<T> from prior arg), and multicast_delegate
//   +=/-= → add/remove (Hover/Goto resolve add/remove; Find References keeps +=/-= use-sites
//   filtered by receiver type).
// - Execution backends (EExecutionBackend: vm / mixed / jit) are orthogonal to ERunMode.
//   DebugSession / DAP accept a preferred backend (launch `backend`, CLI `--backend` via
//   DapStdio defaults). FJitPolicy is a DebugSession / embed Session / VM host API
//   (not a DAP wire field). Attaching the debugger always forces the effective backend
//   to Vm and invalidates JIT (same contract as CLI --debug and embed).
//   MaxCallDepth is a DebugSession setting and DAP launch/attach wire field; 0 resolves
//   VERO_MAX_CALL_DEPTH and then the engine default, matching Session/VM behavior.
//   LSP analysis does not execute bytecode and is backend-agnostic.
// - JIT backends are optional modules, selected by execution hosts through
//   SetJitBackendFactory or SetJitBackendPath (explicit UTF-8 library/directory).
//   LoadJitBackend verifies module/core ABI compatibility and retains the module
//   for factory/artifact lifetimes; it does not discover backends through PATH.
//   Tooling does not load JIT modules. DAP `backend` is an execution preference,
//   not a module selector; live-domain backend selection belongs to its host.
//   Backend identity and native-code capabilities come from the selected factory's
//   IJitBackendFactory::GetDescription(), not the core service-contract identifier.
//   DebugSession forces Vm: GetLastJitStats reports no compile attempts or dispatches;
//   cache invalidation counts depend on the selected backend and cached entries.
// - FJitPolicy / GetLastJitStats: DebugSession and embed VeroScriptSession (not DAP wire).
//   Session.Clear / DebugSession.Clear zero LastJitStats but keep the requested policy.
//   GetLastJitDiagnostics retains the optional per-function snapshot after execution.
//   Collection is disabled by default; Clear or a run with collection disabled clears it.
// - Multi-platform: Tooling itself is OS-portable (PathUtil / file:// URI / VERO_PATH :|;).
//   LSP/DAP initialize expose version + host platform + jitCapability
//   from the core. jitCapability identifies the JIT service contract (currently
//   backend-services); it does not advertise an installed or selected JIT backend.
//   Stdio binary mode (_setmode) stays in CLI DAP/LSP hosts on Windows only.
// - current language surface: Outline/Hover/WorkspaceSymbols/Navigation expose final types/methods,
//   interface fields, and a first-class Destructor kind for `~Type`; interface method kinds
//   (explicit `= 0` abstract / static default / virtual default / virtual final); destructor bodies participate
//   in local scopes; type Rename/FindRefs rewrite or include embedded `~Type` name tokens and
//   same-named constructor decls (direct rename on `~Type` is refused; type+ctor query hits
//   collapse so Rename is not spuriously ambiguous); SignatureHelp lists constructor type
//   overloads (ActiveSignature prefers literal/binding type) and excludes destructor children;
//   Complete/CallHelp filter private/protected members by enclosing type access context and
//   tighten visibility through private/protected inheritance; implementing-type Complete walks
//   interface field/method slices via the type hierarchy. Host binding catalogs may mark types
//   inheritable (class/struct X : Host); Complete/Hover/CallHelp resolve host members via the
//   script type's host base when Bindings are supplied. Completion lists sort by MatchQuality
//   first, then SortPriority from kind + caret context (expression / member / capture / …),
//   and expose the complete ordering through LSP sortText.

#include "VeroScriptAnalyzer.h"
#include "VeroScriptCallHelp.h"
#include "VeroScriptComplete.h"
#include "VeroScriptLiveDebugDomain.h"
#include "VeroScriptDapAdapter.h"
#include "VeroScriptDapStdio.h"
#include "VeroScriptDapTcp.h"
#include "VeroScriptDebugSession.h"
#include "VeroScriptDocumentHighlight.h"
#include "VeroScriptFindReferences.h"
#include "VeroScriptFormat.h"
#include "VeroScriptHover.h"
#include "VeroScriptLspStdio.h"
#include "VeroScriptNavigation.h"
#include "VeroScriptOutline.h"
#include "VeroScriptRename.h"
#include "VeroScriptToolingTypes.h"
#include "VeroScriptWorkspace.h"
#include "VeroScriptWorkspaceSymbols.h"
