// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// A concrete generic specialization follows the ordinary compilation pipeline.
T identity<T>(T value)
{
	return value;
}
int work(int count)
{
	int sum = 0;
	for (int i = 0; i < count; i++)
	{
		sum += i;
	}
	return identity<int>(sum);
}
int main()
{
	print(work(8));
	return 0;
}
