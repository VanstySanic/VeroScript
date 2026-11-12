// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Typed exceptions, catch-all fallback, rethrow, and guaranteed cleanup.
// Run from this directory: veroscript exceptions.vs
// catch handles values raised by `throw`; fatal VM safety errors still propagate,
// but their stack unwinding also executes finally. A finally block may throw or
// use its own nested loops, but may not return/goto or break an enclosing loop.

void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

void load_score(int score)
{
	try
	{
		validate_score(score);
	}
	catch (const string& message)
	{
		print("validation failed: {}", message);
		throw;
	}
	finally
	{
		print("load attempt finished");
	}
}

int main()
{
	try
	{
		load_score(-1);
	}
	catch (string message)
	{
		print("handled: {}", message);
	}
	catch (...)
	{
		print("unexpected script exception");
	}

	return 0;
}
