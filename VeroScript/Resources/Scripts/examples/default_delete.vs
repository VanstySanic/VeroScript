// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Explicit generated and forbidden declarations.
// Run from this directory: veroscript default_delete.vs
// Deleted overloads may hide non-virtual members, but cannot replace a virtual
// or interface slot; virtual dispatch must always have one callable target.

struct Token
{
	// See default_special_members.vs for generated copy and equality operations.
	Token() = default;
	Token(string text) = delete;
	~Token() = default;

	int read() const
	{
		return value;
	}
	int read(int unsupportedMode) const = delete;
	static int normalize(int input)
	{
		return input;
	}
	static int normalize(string text) = delete;

	Token operator+(int amount)
	{
		Token result();
		result.value = value + amount;
		return result;
	}
	Token operator+(string text) = delete;

	int value = 7;
};

int parse_token(int value)
{
	return value;
}
int parse_token(string text) = delete;

void main()
{
	Token token();
	Token advanced = token + 5;
	print("{} {} {}", token.read(), advanced.read(), Token::normalize(parse_token(12)));
}
