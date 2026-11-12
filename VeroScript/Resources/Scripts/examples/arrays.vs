// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// arrays.vs — native T[N] / T[N][M] showcase (+ alias range-for)

import "core.vs";

struct Row
{
	int cells[3] = {1, 2, 3};
};

void bump(int& v)
{
	v = v + 1;
}

void main()
{
	int data[4] = {10, 20, 30, 40};
	print("len = " + to_string(len(data)));
	print("data[2] = " + to_string(data[2]));

	// Alias range-for writes mutations back to the local T[N].
	for (int& n : data)
	{
		n = n + 1;
	}
	print("data[0] after alias range-for = " + to_string(data[0]));

	Row row = Row();
	print("row.cells[0] = " + to_string(row.cells[0]));
	print("row.cells[2] = " + to_string(row.cells[2]));

	// Multidimensional native arrays preserve their fixed-size dimensions.
	int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	print("grid[1][2] = " + to_string(grid[1][2]));
	grid[0][1] = 9;
	bump(&grid[1][0]);
	print("grid[0][1] = " + to_string(grid[0][1]));
	print("grid[1][0] = " + to_string(grid[1][0]));
	for (int& row3[3] : grid)
	{
		row3[2] = row3[2] + 10;
	}
	print("grid[0][2] after row alias = " + to_string(grid[0][2]));
}
