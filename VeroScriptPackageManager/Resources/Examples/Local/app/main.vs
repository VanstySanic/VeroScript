// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "example/math.vs";
int main()
{
	print("answer: {}", answer());
	return answer() == 42 ? 0 : 1;
}
