// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// friend_access.vs - C++-style type and exact-signature function friendship.
// Run from this directory: veroscript friend_access.vs
// Friendship is explicit, not reciprocal, not transitive, and not inherited.

class Auditor
{
public:
	int Read(Vault value) const
	{
		return value.Secret;
	}
};

class Locksmith
{
public:
	int Open(Vault value, int key)
	{
		return value.Secret + key;
	}
};

class Vault
{
	// Every Auditor member may access this type's private/protected members.
	friend class Auditor;

	// Only this exact overload is a friend; other Locksmith methods are not.
	friend int Locksmith::Open(Vault value, int key);

	// An exact free-function friend is also supported.
	friend int ReadSecret(Vault value);

public:
	Vault()
	{
	}

private:
	int Secret = 40;
};

int ReadSecret(Vault value)
{
	return value.Secret;
}

void main()
{
	Vault value = Vault();
	Auditor auditor = Auditor();
	Locksmith locksmith = Locksmith();
	print("type friend = {}", auditor.Read(value));
	print("member friend = {}", locksmith.Open(value, 2));
	print("free friend = {}", ReadSecret(value));
}
