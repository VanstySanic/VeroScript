#include "LineBox.h"
#include "../../../Include/RmlUi/Core/Element.h"
#include "../../../Include/RmlUi/Core/Core.h"
#include "../../../Include/RmlUi/Core/FontEngineInterface.h"
#include "../../../Include/RmlUi/Core/StringUtilities.h"
#include "../../../Include/RmlUi/Core/StyleTypes.h"
#include "InlineBox.h"
#include "InlineLevelBox.h"
#include "LayoutPools.h"
#include <float.h>
#include <algorithm>
#include <limits>
#include <numeric>

namespace Rml {

LineBox::~LineBox() {}

void LineBox::SetLineBox(Vector2f _line_position, float _line_width, float _line_minimum_height)
{
	line_position = _line_position;
	line_width = _line_width;
	line_minimum_height = _line_minimum_height;
}

bool LineBox::AddBox(InlineLevelBox* box, InlineLayoutMode layout_mode, LayoutOverflowHandle& inout_overflow_handle)
{
	RMLUI_ASSERT(!is_closed);
	const bool first_box = !HasContent();

	// Find the spacing this element must leave on its right side, to account for its parent inline boxes to be closed later.
	float open_spacing_right = 0.f;
	ForAllOpenFragments([&](const Fragment& fragment) { open_spacing_right += fragment.box->GetSpacingRight(); });

	const float box_placement_cursor = box_cursor + open_spacing_left;

	float available_width = FLT_MAX;
	if (layout_mode != InlineLayoutMode::Nowrap)
	{
		available_width = Math::RoundUp(line_width - box_placement_cursor);
		if (available_width < 0.f)
		{
			if (layout_mode == InlineLayoutMode::WrapAny)
				return true;
			else
				available_width = 0.f;
		}
	}

	FragmentConstructor constructor = box->CreateFragment(layout_mode, available_width, open_spacing_right, first_box, inout_overflow_handle);

	if (constructor.type == FragmentType::Invalid)
	{
		// Could not place fragment on this line, try again on a new line.
		RMLUI_ASSERT(layout_mode == InlineLayoutMode::WrapAny);
		return true;
	}

	const FragmentIndex fragment_index = (FragmentIndex)fragments.size();

	fragments.push_back(Fragment{box, constructor, box->GetVerticalAlign(), box_placement_cursor, open_fragments_leaf});
	fragments.back().aligned_subtree_root = DetermineAlignedSubtreeRoot(fragment_index);

	inout_overflow_handle = {};
	bool continue_on_new_line = false;

	switch (constructor.type)
	{
	case FragmentType::InlineBox:
	{
		RMLUI_ASSERT(constructor.layout_width < 0.f);
		RMLUI_ASSERT(rmlui_static_cast<InlineBox*>(box));

		open_fragments_leaf = fragment_index;
		open_spacing_left += box->GetSpacingLeft();
	}
	break;
	case FragmentType::SizedBox:
	case FragmentType::TextRun:
	{
		RMLUI_ASSERT(constructor.layout_width >= 0.f);

		box_cursor = box_placement_cursor + constructor.layout_width;
		open_spacing_left = 0.f;

		if (constructor.overflow_handle)
		{
			continue_on_new_line = true;
			inout_overflow_handle = constructor.overflow_handle;
		}

		// Mark open fragments as having content so we later know whether we should split or move them in case of overflow.
		ForAllOpenFragments([&](Fragment& fragment) { fragment.has_content = true; });
		has_content = true;
	}
	break;
	case FragmentType::Invalid:
		RMLUI_ERROR; // Handled above;
		break;
	}

	return continue_on_new_line;
}

void LineBox::CloseFragment(Fragment& open_fragment, float right_inner_edge_position)
{
	RMLUI_ASSERT(open_fragment.type == FragmentType::InlineBox);

	open_fragment.children_end_index = (int)fragments.size();
	const float spacing_left = (open_fragment.split_left ? 0.f : open_fragment.box->GetSpacingLeft());
	open_fragment.layout_width = Math::Max(right_inner_edge_position - open_fragment.position.x - spacing_left, 0.f);
}

void LineBox::CloseInlineBox(InlineBox* inline_box)
{
	if (open_fragments_leaf == RootFragmentIndex || fragments[open_fragments_leaf].box != inline_box)
	{
		RMLUI_ERRORMSG("Inline box open/close mismatch.");
		return;
	}

	box_cursor += open_spacing_left;
	open_spacing_left = 0.f;

	Fragment& fragment = fragments[open_fragments_leaf];
	CloseFragment(fragment, box_cursor);
	box_cursor += fragment.box->GetSpacingRight();

	open_fragments_leaf = fragment.parent;
}

UniquePtr<LineBox> LineBox::SplitLine(bool split_all_open_boxes)
{
	if (open_fragments_leaf == RootFragmentIndex)
		return nullptr;

	int num_open_fragments = 0;
	ForAllOpenFragments([&](const Fragment& /*fragment*/) { num_open_fragments += 1; });

	// Move the box cursor to account for any newly opened inline boxes to be closed.
	box_cursor += open_spacing_left;
	open_spacing_left = 0.f;

	// Make a new line with the open fragments.
	auto new_line = MakeUnique<LineBox>();
	new_line->fragments.resize(num_open_fragments);

	// Copy all open fragments to the next line. Do it in reverse order of iteration, since we iterate from back to front.
	FragmentIndex new_index = num_open_fragments;
	ForAllOpenFragments([&](Fragment& old_fragment) {
		new_index -= 1;
		RMLUI_ASSERT((size_t)new_index < new_line->fragments.size() && old_fragment.children_end_index == 0);

		// Copy the old fragment.
		Fragment& new_fragment = new_line->fragments[new_index];
		new_fragment = old_fragment;
		new_fragment.position.x = 0.f;
		new_fragment.parent = new_index - 1;
		new_fragment.aligned_subtree_root = new_line->DetermineAlignedSubtreeRoot(new_index);

		// Only fragments with content placed on the previous line is split, otherwise the fragment is moved down.
		if (new_fragment.has_content || split_all_open_boxes)
		{
			new_fragment.split_left = true;
			new_fragment.has_content = false;

			CloseFragment(old_fragment, box_cursor);
			old_fragment.split_right = true;
		}
		else
		{
			const float spacing_left = (new_fragment.split_left ? 0.f : new_fragment.box->GetSpacingLeft());
			new_line->open_spacing_left += spacing_left;

			// The old fragment is not closed here, which ensures that it will not be placed/submitted on the previous
			// line. Backtrack the box cursor since this fragment is moved down to the next line.
			box_cursor -= spacing_left;
		}
	});

	new_line->open_fragments_leaf = (int)new_line->fragments.size() - 1;
	open_fragments_leaf = RootFragmentIndex;

#ifdef RMLUI_DEBUG
	// Verify integrity of the fragment tree after split.
	for (int i = 0; i < (int)new_line->fragments.size(); i++)
	{
		const Fragment& fragment = new_line->fragments[i];
		RMLUI_ASSERT(fragment.type == FragmentType::InlineBox);
		RMLUI_ASSERT(fragment.parent < i);
		RMLUI_ASSERT(fragment.parent == i - 1);
		RMLUI_ASSERT(fragment.parent == RootFragmentIndex || new_line->fragments[fragment.parent].type == FragmentType::InlineBox);
		RMLUI_ASSERT(
			fragment.aligned_subtree_root == RootFragmentIndex || new_line->IsAlignedSubtreeRoot(new_line->fragments[fragment.aligned_subtree_root]));
		RMLUI_ASSERT(fragment.children_end_index == 0);
	}
#endif

	return new_line;
}

UniquePtr<LineBox> LineBox::DetermineVerticalPositioning(const InlineBoxRoot* root_inline_box, bool split_all_open_boxes, float& out_height_of_line)
{
	RMLUI_ASSERT(!is_closed && !is_vertically_positioned);

	UniquePtr<LineBox> new_line_box = SplitLine(split_all_open_boxes);

	RMLUI_ASSERT(open_fragments_leaf == RootFragmentIndex); // Ensure all open fragments are either closed or split.

	// Vertical alignment and sizing.
	//
	// Aligned subtree CSS definition: "The aligned subtree of an inline element contains that element and the aligned
	// subtrees of all children inline elements whose computed vertical-align value is not 'top' or 'bottom'."
	//
	// We have already determined each box's offset relative to its parent baseline, and its layout size above and below
	// its baseline. Now, for each aligned subtree, place all fragments belonging to that subtree relative to the
	// subtree root baseline. Simultaneously, consider each fragment and keep track of the maximum height above root
	// baseline (max_ascent) and maximum depth below root baseline (max_descent). Their sum is the height of that
	// aligned subtree.
	//
	// Next, treat the root inline box like just another aligned subtree. Then the line box height is first determined
	// by the height of that subtree. Other aligned subtrees might push out that size to make them fit. After the line
	// box size is determined, position each aligned subtree according to its vertical-align, and then position each
	// fragment relative to the aligned subtree they belong to.
	float max_ascent = root_inline_box->GetHeightAboveBaseline();
	float max_descent = root_inline_box->GetDepthBelowBaseline();

	{
		const int subtree_root_index = RootFragmentIndex;
		const int children_end_index = (int)fragments.size();
		VerticallyAlignSubtree(subtree_root_index, children_end_index, max_ascent, max_descent);
	}

	// Find all the aligned subtrees, and vertically align each of them independently.
	for (int i = 0; i < (int)fragments.size(); i++)
	{
		Fragment& fragment = fragments[i];
		if (IsAlignedSubtreeRoot(fragment))
		{
			fragment.max_ascent = fragment.box->GetHeightAboveBaseline();
			fragment.max_descent = fragment.box->GetDepthBelowBaseline();

			if (fragment.type == FragmentType::InlineBox)
			{
				const int subtree_root_index = i;
				VerticallyAlignSubtree(subtree_root_index, fragment.children_end_index, fragment.max_ascent, fragment.max_descent);
			}

			const float subtree_height = fragment.max_ascent + fragment.max_descent;

			// Increase the line box size to fit all line-relative aligned fragments.
			switch (fragment.vertical_align)
			{
			case VerticalAlignType::Top: max_descent = Math::Max(max_descent, subtree_height - max_ascent); break;
			case VerticalAlignType::Bottom: max_ascent = Math::Max(max_ascent, subtree_height - max_descent); break;
			case VerticalAlignType::Center:
			{
				// Distribute the subtree's height equally to the ascent and descent.
				const float distribute_height = 0.5f * (subtree_height - (max_ascent + max_descent));
				if (distribute_height > 0.f)
				{
					max_ascent += distribute_height;
					max_descent += distribute_height;
				}
			}
			break;
			default: RMLUI_ERROR; break;
			}
		}
	}

	// Size the line.
	out_height_of_line = max_ascent + max_descent;
	total_height_above_baseline = max_ascent;

	// Now that the line is sized we can set the vertical position of the fragments.
	for (Fragment& fragment : fragments)
	{
		switch (fragment.vertical_align)
		{
		case VerticalAlignType::Top: fragment.position.y = fragment.max_ascent; break;
		case VerticalAlignType::Bottom: fragment.position.y = out_height_of_line - fragment.max_descent; break;
		case VerticalAlignType::Center: fragment.position.y = 0.5f * (fragment.max_ascent - fragment.max_descent + out_height_of_line); break;
		default:
		{
			RMLUI_ASSERT(!IsAlignedSubtreeRoot(fragment));
			const float aligned_subtree_baseline =
				(fragment.aligned_subtree_root < 0 ? max_ascent : fragments[fragment.aligned_subtree_root].position.y);
			fragment.position.y = aligned_subtree_baseline + fragment.baseline_offset;
		}
		break;
		}
	}

	is_vertically_positioned = true;
	has_visual_fragments = BuildVisualFragments();

	return new_line_box;
}

bool LineBox::BuildVisualFragments()
{
	struct LogicalFragment { int original, begin, end; };
	Vector<LogicalFragment> logical;
	SharedPtr<const String> paragraph;
	Style::Direction direction = Style::Direction::Auto;
	int line_begin = std::numeric_limits<int>::max(), line_end = 0;
	Vector<bool> has_logical_content(fragments.size(), false);
	Vector<bool> has_visual_content(fragments.size(), false);
	for (int i = int(fragments.size()) - 1; i >= 0; --i)
	{
		const auto& fragment = fragments[size_t(i)];
		auto propagate = [&](bool logical_content, bool visual_content) {
			if (fragment.parent >= 0)
			{
				has_logical_content[size_t(fragment.parent)] = has_logical_content[size_t(fragment.parent)] || logical_content;
				has_visual_content[size_t(fragment.parent)] = has_visual_content[size_t(fragment.parent)] || visual_content;
			}
		};
		if (fragment.type == FragmentType::InlineBox && (fragment.children_end_index == 0 || has_logical_content[size_t(i)]))
		{
			propagate(has_logical_content[size_t(i)], has_visual_content[size_t(i)]);
			continue;
		}
		int begin = fragment.box->GetParagraphOffset();
		int length = fragment.box->GetParagraphLength();
		const auto& owner = fragment.box->GetParagraph();
		if (fragment.type == FragmentType::TextRun)
		{
			const auto text_box = static_cast<InlineLevelBox_Text*>(fragment.box);
			begin = text_box->GetFragmentOffset(fragment.fragment_handle);
			length = int(text_box->GetFragmentText(fragment.fragment_handle).size());
		}
		if (!length && fragment.type != FragmentType::InlineBox)
			continue;
		if (!owner || (paragraph && owner != paragraph) || begin < 0 || length < 0 ||
			size_t(begin) > owner->size() || size_t(length) > owner->size() - size_t(begin))
			return false;
		if (fragment.type == FragmentType::InlineBox)
		{
			auto placeholder = owner->find("\xef\xbf\xbc", size_t(begin));
			if (placeholder != String::npos && placeholder + 3 > size_t(begin + length))
				placeholder = String::npos;
			// A span containing only empty/collapsed TextRun descendants still has
			// decoration and one neutral placeholder. Only the innermost such span
			// claims it; ancestors are represented by that same visual item.
			bool neutral_only = true;
			for (size_t cursor = size_t(begin); cursor < size_t(begin + length);)
			{
				if (cursor == placeholder)
					cursor += 3;
				else if (cursor + 3 <= size_t(begin + length) && (*owner)[cursor] == '\xe2' &&
					(*owner)[cursor + 1] == '\x81' && static_cast<unsigned char>((*owner)[cursor + 2]) >= 0xa6 &&
					static_cast<unsigned char>((*owner)[cursor + 2]) <= 0xa9)
					cursor += 3;
				else { neutral_only = false; break; }
			}
			if (!neutral_only)
				return false;
			if (placeholder == String::npos && has_visual_content[size_t(i)])
			{
				propagate(false, true);
				continue;
			}
			if (placeholder != String::npos)
			{
				begin = int(placeholder);
				length = 3;
			}
			else
			{
				// A transparent empty inline still needs a zero-width box submission
				// to discard its old background, but must not interrupt text joining.
				if (length >= 3 && static_cast<unsigned char>((*owner)[size_t(begin)]) == 0xe2 &&
					static_cast<unsigned char>((*owner)[size_t(begin + 2)]) >= 0xa6 &&
					static_cast<unsigned char>((*owner)[size_t(begin + 2)]) <= 0xa8)
					begin += 3;
				length = 0;
			}
		}
		if (!paragraph)
		{
			paragraph = owner;
			direction = fragment.box->GetParagraphDirection();
		}
		line_begin = Math::Min(line_begin, begin);
		line_end = Math::Max(line_end, begin + length);
		logical.push_back({i, begin, begin + length});
		has_logical_content[size_t(i)] = length > 0;
		has_visual_content[size_t(i)] = true;
		propagate(length > 0, true);
	}
	if (logical.empty())
		return false;
	// Include zero-width isolate controls at intact inline edges. Split inline
	// edges must not pull text from a preceding/following soft-wrapped line in.
	for (const auto& fragment : fragments)
	{
		if (fragment.type != FragmentType::InlineBox || fragment.children_end_index == 0 || fragment.box->GetParagraph() != paragraph)
			continue;
		if (!fragment.split_left)
			line_begin = Math::Min(line_begin, fragment.box->GetParagraphOffset());
		if (!fragment.split_right)
			line_end = Math::Max(line_end, fragment.box->GetParagraphOffset() + fragment.box->GetParagraphLength());
	}
	Vector<TextBidiRun> runs;
	if (!GetFontEngineInterface()->GetParagraphRuns(StringView(*paragraph), direction, line_begin, line_end - line_begin, runs, paragraph))
		return false; // Unmodified upstream font engines retain their original layout behavior.
	std::sort(logical.begin(), logical.end(), [](const auto& a, const auto& b) {
		if (a.begin != b.begin) return a.begin < b.begin;
		if (a.end != b.end) return a.end < b.end;
		return a.original < b.original;
	});
	struct Piece { int original, begin, length; };
	Vector<Piece> pieces;
	Vector<int> covered(fragments.size(), 0);
	Vector<bool> placed_empty(fragments.size(), false);
	if (line_begin == line_end)
		for (const auto& value : logical)
		{
			pieces.push_back({value.original, value.begin, 0});
			placed_empty[size_t(value.original)] = true;
		}
	for (const auto& run : runs)
	{
		if (run.byte_offset < line_begin || run.byte_length < 0 || run.byte_offset > line_end || run.byte_length > line_end - run.byte_offset)
			return false;
		const auto first_piece = pieces.size();
		const int run_end = run.byte_offset + run.byte_length;
		auto it = std::lower_bound(logical.begin(), logical.end(), run.byte_offset,
			[](const auto& value, int offset) { return value.end < offset || (value.end == offset && value.begin != value.end); });
		for (; it != logical.end() && (it->begin < run_end || (it->begin == line_end && run_end == line_end)); ++it)
		{
			if (it->begin == it->end)
			{
				if (!placed_empty[size_t(it->original)])
				{
					pieces.push_back({it->original, it->begin, 0});
					placed_empty[size_t(it->original)] = true;
				}
				continue;
			}
			const int begin = Math::Max(it->begin, run.byte_offset);
			const int end = Math::Min(it->end, run_end);
			if (begin >= end)
				continue;
			if (fragments[size_t(it->original)].type != FragmentType::TextRun && (begin != it->begin || end != it->end))
				return false; // Atomic objects cannot be split.
			pieces.push_back({it->original, begin, end - begin});
			covered[size_t(it->original)] += end - begin;
		}
		if (run.level & 1)
			std::reverse(pieces.begin() + first_piece, pieces.end());
	}
	for (const auto& value : logical)
		if (covered[size_t(value.original)] != value.end - value.begin ||
			(value.begin == value.end && !placed_empty[size_t(value.original)]))
			return false;

	struct Item {
		int original;
		LayoutFragmentHandle handle;
		float width, x = 0.f;
		Vector<int> ancestors, left_edges, right_edges;
	};
	Vector<Item> items;
	Vector<int> first(fragments.size(), -1), last(fragments.size(), -1);
	for (const auto& piece : pieces)
	{
		const auto& source = fragments[size_t(piece.original)];
		Item item{piece.original, source.fragment_handle, source.type == FragmentType::InlineBox ? 0.f : source.layout_width};
		if (source.type == FragmentType::TextRun)
		{
			auto text_box = static_cast<InlineLevelBox_Text*>(source.box);
			item.handle = text_box->CreateVisualFragment(source.fragment_handle,
				piece.begin - text_box->GetFragmentOffset(source.fragment_handle), piece.length,
				line_begin, line_end - line_begin, item.width);
		}
		int ancestor = (source.type == FragmentType::InlineBox ? piece.original : source.parent);
		while (ancestor != RootFragmentIndex)
		{
			item.ancestors.push_back(ancestor);
			if (first[size_t(ancestor)] < 0)
				first[size_t(ancestor)] = int(items.size());
			last[size_t(ancestor)] = int(items.size());
			ancestor = fragments[size_t(ancestor)].parent;
		}
		std::reverse(item.ancestors.begin(), item.ancestors.end());
		items.push_back(std::move(item));
	}
	// Preserve physical padding/border edges once on the first/last visual piece
	// of each inline box. Outer edges precede inner edges, and close in reverse.
	for (int i = 0; i < int(fragments.size()); ++i)
	{
		const auto& fragment = fragments[size_t(i)];
		if (fragment.type != FragmentType::InlineBox || first[size_t(i)] < 0)
			continue;
		if (!fragment.split_left)
			items[size_t(first[size_t(i)])].left_edges.push_back(i);
		if (!fragment.split_right)
			items[size_t(last[size_t(i)])].right_edges.push_back(i);
	}
	struct Segment { float left, right; };
	Vector<Vector<Segment>> segments(fragments.size());
	Vector<int> seen(fragments.size(), -2);
	float cursor = 0.f;
	for (int i = 0; i < int(items.size()); ++i)
	{
		auto& item = items[size_t(i)];
		for (const int edge : item.left_edges)
			cursor += fragments[size_t(edge)].box->GetSpacingLeft();
		item.x = cursor;
		cursor += item.width;
		for (const int edge : item.right_edges)
			cursor += fragments[size_t(edge)].box->GetSpacingRight();
		for (const int ancestor : item.ancestors)
		{
			float left = item.x, right = item.x + item.width;
			for (const int edge : item.left_edges)
				if (edge >= ancestor)
					left -= fragments[size_t(edge)].box->GetSpacingLeft();
			for (const int edge : item.right_edges)
				if (edge >= ancestor)
					right += fragments[size_t(edge)].box->GetSpacingRight();
			auto& boxes = segments[size_t(ancestor)];
			if (seen[size_t(ancestor)] == i - 1 || (!boxes.empty() && boxes.back().right == left))
				boxes.back().right = right;
			else
				boxes.push_back({left, right});
			seen[size_t(ancestor)] = i;
		}
	}
	Vector<VisualFragment> result;
	// Submit box fragments before leaf geometry, retaining parent-before-child
	// layout callbacks even when a styled span has discontiguous visual pieces.
	for (int i = 0; i < int(fragments.size()); ++i)
	{
		const auto& source = fragments[size_t(i)];
		const auto& boxes = segments[size_t(i)];
		for (size_t j = 0; j < boxes.size(); ++j)
		{
			const bool split_left = source.split_left || j > 0;
			const bool split_right = source.split_right || j + 1 < boxes.size();
			const float edges = (split_left ? 0.f : source.box->GetSpacingLeft()) + (split_right ? 0.f : source.box->GetSpacingRight());
			result.push_back({i, source.fragment_handle, {boxes[j].left, source.position.y},
				Math::Max(0.f, boxes[j].right - boxes[j].left - edges), split_left, split_right});
		}
	}
	for (const auto& item : items)
	{
		const auto& source = fragments[size_t(item.original)];
		if (source.type != FragmentType::InlineBox)
			result.push_back({item.original, item.handle, {item.x, source.position.y}, item.width, false, false});
	}
	visual_fragments = std::move(result);
	box_cursor = cursor;
	return true;
}

void LineBox::Close(Element* offset_parent, Vector2f offset_parent_position, Style::TextAlign text_align)
{
	RMLUI_ASSERT(is_vertically_positioned && !is_closed);

	// Horizontal alignment using available space on our line.
	if (box_cursor < line_width)
	{
		switch (text_align)
		{
		case Style::TextAlign::Center: offset_horizontal_alignment = (line_width - box_cursor) * 0.5f; break;
		case Style::TextAlign::Right: offset_horizontal_alignment = (line_width - box_cursor); break;
		case Style::TextAlign::Left:    // Already left-aligned.
		case Style::TextAlign::Justify: // Justification occurs at the text level.
			break;
		}
	}

	// Position and size all inline-level boxes, place geometry boxes.
	if (has_visual_fragments)
	{
		for (const auto& fragment : visual_fragments)
			fragments[size_t(fragment.original)].box->Submit({offset_parent, fragment.handle,
				line_position - offset_parent_position + fragment.position + Vector2f(offset_horizontal_alignment, 0.f),
				fragment.width, fragment.split_left, fragment.split_right});
	}
	else
	{
		for (const Fragment& fragment : fragments)
		{
			// Skip inline boxes which have not been closed (moved down to next line).
			if (fragment.type == FragmentType::InlineBox && fragment.children_end_index == 0)
				continue;

			RMLUI_ASSERT(fragment.layout_width >= 0.f);

			const PlacedFragment placed_fragment = {
				offset_parent,
				fragment.fragment_handle,
				line_position - offset_parent_position + fragment.position + Vector2f(offset_horizontal_alignment, 0.f),
				fragment.layout_width,
				fragment.split_left,
				fragment.split_right,
			};
			fragment.box->Submit(placed_fragment);
		}
	}

	is_closed = true;
}

void LineBox::VerticallyAlignSubtree(const int subtree_root_index, const int children_end_index, float& max_ascent, float& max_descent)
{
	const int children_begin_index = subtree_root_index + 1;

	// Iterate all descendant fragments which belong to our aligned subtree.
	for (int i = children_begin_index; i < children_end_index; i++)
	{
		Fragment& fragment = fragments[i];
		if (fragment.aligned_subtree_root != subtree_root_index)
			continue;

		// Position the baseline of fragments relative to their subtree root.
		const float parent_absolute_baseline = (fragment.parent < 0 ? 0.f : fragments[fragment.parent].baseline_offset);
		fragment.baseline_offset = parent_absolute_baseline + fragment.box->GetVerticalOffsetFromParent();

		// Expand this aligned subtree's height based on the height contributions of its descendants.
		if (fragment.type != FragmentType::TextRun)
		{
			max_ascent = Math::Max(max_ascent, fragment.box->GetHeightAboveBaseline() - fragment.baseline_offset);
			max_descent = Math::Max(max_descent, fragment.box->GetDepthBelowBaseline() + fragment.baseline_offset);
		}
	}
}

InlineBox* LineBox::GetOpenInlineBox()
{
	if (open_fragments_leaf == RootFragmentIndex)
		return nullptr;

	return rmlui_static_cast<InlineBox*>(fragments[open_fragments_leaf].box);
}

bool LineBox::CanCollapseLine() const
{
	// Roughly, collapse lines with only empty text fragments or inline boxes not taking up any width or spacing.
	for (const Fragment& fragment : fragments)
	{
		if (fragment.layout_width > 0.f)
			return false;
		else if (fragment.type == FragmentType::SizedBox)
			return false;
		else if (fragment.type == FragmentType::InlineBox && fragment.children_end_index > 0)
		{
			const bool any_spacing_left = (!fragment.split_left && fragment.box->GetSpacingLeft() != 0.f);
			const bool any_spacing_right = (!fragment.split_right && fragment.box->GetSpacingRight() != 0.f);
			if (any_spacing_left || any_spacing_right)
				return false;
		}
	}
	return true;
}

float LineBox::GetExtentRight() const
{
	RMLUI_ASSERT(is_closed);
	return box_cursor + offset_horizontal_alignment;
}

float LineBox::GetBaseline() const
{
	RMLUI_ASSERT(is_closed);
	return total_height_above_baseline;
}

String LineBox::DebugDumpTree(int depth) const
{
	const String value = String(depth * 2, ' ') + "LineBox (" + ToString(fragments.size()) + " fragment" + (fragments.size() == 1 ? "" : "s") + ")\n";
	return value;
}

void* LineBox::operator new(size_t size)
{
	return LayoutPools::AllocateLayoutChunk(size);
}

void LineBox::operator delete(void* chunk, size_t size)
{
	LayoutPools::DeallocateLayoutChunk(chunk, size);
}

} // namespace Rml
