// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import compiler "builder.vs";

@ builder class Card
{
public:
	string text;
};

int main()
{
	Card card = Card::Create().Text("Save").Build();
	print(card.text);
	return 0;
}
