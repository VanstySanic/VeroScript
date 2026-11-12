// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Normal source checks use the source module. Define VERO_USE_BINARY_PACKAGE
// when building the application package to consume the compiled .vslib instead.

#ifdef VERO_USE_BINARY_PACKAGE
import "DemoLibrary.vslib";
#else
import "package_library.vs";
#endif
class NumberReader : IReadable<int>
{
public:
	int read() const
	{
		return 40;
	}
};

void main()
{
	Box<int> box(40);
	NumberReader reader();
	Operation operation = Operation::Add;

	print(identity<int>(box.get()) + shared_offset);
	print(reader.read() + shared_offset);
	print(operation);
}
