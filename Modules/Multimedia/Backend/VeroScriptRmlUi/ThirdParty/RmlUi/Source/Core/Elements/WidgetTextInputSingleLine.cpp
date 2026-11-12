#include "WidgetTextInputSingleLine.h"
#include "../../../Include/RmlUi/Core/Dictionary.h"
#include "../../../Include/RmlUi/Core/ElementText.h"
#include "../../../Include/RmlUi/Core/Elements/ElementFormControl.h"
#include "../../../Include/RmlUi/Core/StringUtilities.h"

namespace Rml {

WidgetTextInputSingleLine::WidgetTextInputSingleLine(ElementFormControl* parent) : WidgetTextInput(parent)
{
	// Single line text controls should clip to the content area, see visual test: text_input_overflow.rml
	parent->SetClipArea(BoxArea::Content);
}

void WidgetTextInputSingleLine::SanitizeValue(String& value)
{
	size_t written = 0;
	for (size_t read = 0; read < value.size();)
	{
		const size_t line_break = StringUtilities::GetLineBreakLength(value.data() + read, value.data() + value.size());
		if (line_break || value[read] == '\t')
			read += line_break ? line_break : 1;
		else
			value[written++] = value[read++];
	}
	value.resize(written);
}

void WidgetTextInputSingleLine::LineBreak()
{
	DispatchChangeEvent(true);
}

} // namespace Rml
