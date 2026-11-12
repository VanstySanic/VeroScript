// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

void sort_inner(array<int> a, int n)
{
	for (int64 start = n / 2 - 1; start >= 0; start--)
	{
		int64 root = start;
		while (root < n / 2)
		{
			int64 child = root * 2 + 1;
			int64 largest = root;
			if (a[largest] < a[child])
			{
				largest = child;
			}
			if (child + 1 < n && a[largest] < a[child + 1])
			{
				largest = child + 1;
			}
			if (largest == root)
			{
				break;
			}
			int v = a[root];
			a[root] = a[largest];
			a[largest] = v;
			root = largest;
		}
	}
	for (int64 end = n - 1; end > 0; end--)
	{
		int v = a[0];
		a[0] = a[end];
		a[end] = v;
		int64 root = 0;
		while (root < end / 2)
		{
			int64 child = root * 2 + 1;
			int64 largest = root;
			if (a[largest] < a[child])
			{
				largest = child;
			}
			if (child + 1 < end && a[largest] < a[child + 1])
			{
				largest = child + 1;
			}
			if (largest == root)
			{
				break;
			}
			v = a[root];
			a[root] = a[largest];
			a[largest] = v;
			root = largest;
		}
	}
}
int work(int n, int seed)
{
	array<int> a = array<int>();
	int s = seed;
	for (int i = 0; i < n; i++)
	{
		s = (s * 75 + 74) % 65521;
		a.push(s);
	}
	sort_inner(a, n);
	return a[0] + a[n / 2] + a[n - 1];
}

void main()
{
	print(work(3000, 17));
}
