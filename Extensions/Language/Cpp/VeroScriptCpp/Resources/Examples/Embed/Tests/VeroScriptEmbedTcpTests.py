# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Exercise real embedded TCP bindings with bounded I/O and child cleanup."""

import json
from pathlib import Path
import queue
import re
import socket
import subprocess
import sys
import threading
import time


class Client:
	def __init__(self, port):
		self.socket = socket.create_connection(("127.0.0.1", port), timeout=5)
		self.stream = self.socket.makefile("rb")
		self.sequence = 0
		self.output = ""
		self.terminated = False

	def close(self):
		self.stream.close()
		self.socket.close()

	def send(self, command, arguments):
		self.sequence += 1
		body = json.dumps({"seq": self.sequence, "type": "request",
						   "command": command, "arguments": arguments}).encode()
		self.socket.sendall(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
		return self.sequence

	def read(self, deadline):
		self.socket.settimeout(max(0.001, deadline - time.monotonic()))
		length = None
		while True:
			line = self.stream.readline(4096)
			if line == b"\r\n":
				break
			if not line:
				raise AssertionError("DAP stream closed before response")
			name, value = line.decode("ascii").strip().split(":", 1)
			if name.lower() == "content-length":
				length = int(value)
		if length is None or not 0 < length <= 1024 * 1024:
			raise AssertionError("invalid DAP content length")
		body = self.stream.read(length)
		if len(body) != length:
			raise AssertionError("truncated DAP message")
		message = json.loads(body)
		if message.get("event") == "output":
			self.output += message.get("body", {}).get("output", "")
		if message.get("event") == "terminated":
			self.terminated = True
		return message

	def request(self, command, arguments, success=True):
		sequence = self.send(command, arguments)
		deadline = time.monotonic() + 10
		while time.monotonic() < deadline:
			message = self.read(deadline)
			if message.get("type") == "response" and message.get("request_seq") == sequence:
				if message.get("success") != success:
					raise AssertionError(f"unexpected {command} response: {message}")
				return message
		raise AssertionError(f"no {command} response")

	def wait_terminated(self):
		deadline = time.monotonic() + 10
		while not self.terminated and time.monotonic() < deadline:
			self.read(deadline)
		if not self.terminated:
			raise AssertionError("execution did not terminate")


def main():
	executable = str(Path(sys.argv[1]).resolve())
	for invalid in ("-1", "65536", "4711junk"):
		result = subprocess.run([executable, "dap-tcp", invalid],
								capture_output=True, timeout=5)
		if result.returncode == 0:
			raise AssertionError(f"accepted invalid port {invalid}")

	process = subprocess.Popen([executable, "dap-tcp", "0"],
							   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
							   text=True, encoding="utf-8", errors="replace")
	messages = queue.Queue()
	diagnostics = []

	def read_stderr():
		for line in process.stderr:
			diagnostics.append(line)
			messages.put(line)
		messages.put(None)

	reader = threading.Thread(target=read_stderr, daemon=True)
	reader.start()
	try:
		deadline = time.monotonic() + 10
		port = None
		while port is None:
			line = messages.get(timeout=max(0.001, deadline - time.monotonic()))
			if line is None:
				raise AssertionError("host exited before listening")
			match = re.search(r"listening on 127\.0\.0\.1:(\d+)", line)
			if match:
				port = int(match.group(1))

		# The first detach leaves the host listening; the second explicitly
		# stops this standalone demo process. Neither is a UE/PIE operation.
		for terminate_host in (False, True):
			client = Client(port)
			try:
				client.request("initialize", {})
				client.request("attach", {
					"source": "void main() { print(host_add(11, 31)); }\n",
					"fileLabel": "embed-tcp.vs", "stopOnEntry": False})
				client.request("configurationDone", {})
				client.wait_terminated()
				if "42" not in client.output:
					raise AssertionError(f"real host binding did not return 42: {client.output!r}")
				client.request("disconnect", {"terminateDebuggee": terminate_host})
			finally:
				client.close()
		if process.wait(timeout=5) != 0:
			raise AssertionError("embedded host exited with an error")
	except BaseException:
		print("".join(diagnostics), file=sys.stderr)
		raise
	finally:
		if process.poll() is None:
			process.kill()
		process.wait(timeout=5)
		reader.join(timeout=5)
		process.stdout.close()
		process.stderr.close()


if __name__ == "__main__":
	main()
