// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Deployed asset for the repository-integrated embedding example.
// Embedding demo — requires the host_add and host_log native functions.

void main()
{
	int total = host_add(3, 4);
	print(total);
	host_log("embed-file");
	print("embed-file");
}
