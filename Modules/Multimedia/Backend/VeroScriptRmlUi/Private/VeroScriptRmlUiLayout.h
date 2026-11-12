// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptGuiBackend.h"
#include <unordered_map>

namespace Rml
{
	class Context;
	class Element;
} // namespace Rml

namespace VeroScript::RmlUi
{
	using FLayoutElements = std::unordered_map<std::uint64_t, Rml::Element *>;
	bool UpdateContainerLayouts(Rml::Context &Context, const FLayoutElements &Elements,
								const std::vector<Gui::FBackendNode> &Nodes,
								const std::unordered_map<std::uint64_t, std::size_t> &NodeIndices,
								const Gui::FViewport &Viewport,
								std::string &Error);
	bool PositionFloatingControls(Rml::Context &Context, const FLayoutElements &Elements,
								  const std::vector<Gui::FBackendNode> &Nodes, const Gui::FViewport &Viewport,
								  std::string &Error);
} // namespace VeroScript::RmlUi
