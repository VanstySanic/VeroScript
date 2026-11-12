// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load network. Pass one existing absolute directory. Replaces only veroscript-file-example.bin in that directory.
int main(string[] args)
{
	if (len(args) != 1) return 1;
	Network::context context = Network::create_context(false);
	Network::file_executor files = Network::create_file_executor(context);
	string expected = "hello\0file";
	Network::byte_pipe pipe = Network::create_byte_pipe(context, 64);
	if (pipe.writer().write(expected) != len(expected)) return 2;
	pipe.writer().finish();
	Network::file_write_options output = Network::make_file_write_options("veroscript-file-example.bin", args[0]);
	output.set_expected_length(len(expected));
	output.set_commit_options(true, false, true);
	Network::file_operation write = files.write(output, pipe.reader());
	Network::operation written = write.operation();
	if (written.wait(10000) != 0 || written.error_code() != 0 || write.progress().commit_state() != 2) return 3;

	Network::file_read_options input = Network::make_file_read_options("veroscript-file-example.bin", args[0]);
	input.set_timeout(10000);
	Network::file_operation read = files.read(input);
	Network::readable_stream bytes = read.body();
	string actual = "";
	for (int round = 0; round < 10000 && bytes.state() != 2 && bytes.state() != 3; ++round)
	{
		actual += bytes.read(4);
		if (bytes.state() == 1)
		{
			std::this_thread::sleep(1);
		}
	}
	// Drain the bounded body before waiting for the producer.
	if (bytes.state() != 2 || actual != expected || read.operation().wait(10000) != 0 || read.operation().error_code() != 0) return 4;
	files.close();
	context.shutdown();
	return 0;
}
