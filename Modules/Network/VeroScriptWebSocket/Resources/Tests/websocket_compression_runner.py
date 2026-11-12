# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""RFC 7692 peers using Python zlib, independent of the provider's Beast codecs."""
import base64
import hashlib
import os
from pathlib import Path
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import threading
import zlib

from websocket_proxy_runner import exact, read_head


def frame(connection, opcode, payload, masked=False, compressed=False, final=True):
	header = bytes([(0x80 if final else 0) | (0x40 if compressed else 0) | opcode])
	count = len(payload)
	flag = 0x80 if masked else 0
	if count < 126:
		header += bytes([flag | count])
	elif count <= 65535:
		header += bytes([flag | 126]) + struct.pack("!H", count)
	else:
		header += bytes([flag | 127]) + struct.pack("!Q", count)
	if masked:
		mask = b"1234"
		header += mask
		payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
	connection.sendall(header + payload)


def read_frame(connection, masked):
	a, b = exact(connection, 2)
	if a & 0x30 or bool(b & 0x80) != masked:
		raise ValueError("invalid frame flags or masking direction")
	count = b & 127
	if count == 126:
		count = struct.unpack("!H", exact(connection, 2))[0]
	elif count == 127:
		count = struct.unpack("!Q", exact(connection, 8))[0]
	if count > 2 * 1024 * 1024:
		raise ValueError("fixture frame limit exceeded")
	mask = exact(connection, 4) if masked else None
	payload = exact(connection, count)
	if mask:
		payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
	return a & 15, payload, bool(a & 0x80), bool(a & 0x40)


def read_message(connection, masked):
	chunks, first_opcode, compressed = [], None, False
	while True:
		opcode, payload, final, zipped = read_frame(connection, masked)
		if opcode >= 8:
			if not final or zipped or len(payload) > 125:
				raise ValueError("invalid control frame")
			if opcode == 9:
				frame(connection, 10, payload, not masked)
			elif opcode == 8:
				return opcode, payload, False
			elif opcode != 10:
				raise ValueError("unknown control frame")
			continue
		if first_opcode is None:
			if opcode not in (1, 2):
				raise ValueError("missing first data frame")
			first_opcode, compressed = opcode, zipped
		elif opcode != 0 or zipped:
			raise ValueError("invalid continuation frame")
		chunks.append(payload)
		if sum(map(len, chunks)) > 2 * 1024 * 1024:
			raise ValueError("fixture message limit exceeded")
		if final:
			return first_opcode, b"".join(chunks), compressed


class Codec:
	def __init__(self, enabled, output_bits=15, input_bits=15, output_fresh=False, input_fresh=False):
		self.enabled, self.output_bits, self.input_bits = enabled, output_bits, input_bits
		self.output_fresh, self.input_fresh = output_fresh, input_fresh
		self.encoder = self.decoder = None

	def encode(self, data):
		if not self.enabled:
			return data
		if self.encoder is None or self.output_fresh:
			self.encoder = zlib.compressobj(wbits=-self.output_bits)
		encoded = self.encoder.compress(data) + self.encoder.flush(zlib.Z_SYNC_FLUSH)
		if not encoded.endswith(b"\0\0\xff\xff"):
			raise ValueError("missing DEFLATE flush tail")
		return encoded[:-4]

	def decode(self, data, compressed):
		if not compressed:
			return data
		if not self.enabled:
			raise ValueError("compressed message without negotiation")
		if self.decoder is None or self.input_fresh:
			self.decoder = zlib.decompressobj(wbits=-self.input_bits)
		result = self.decoder.decompress(data + b"\0\0\xff\xff", 128 * 1024 + 1)
		if len(result) > 128 * 1024 or self.decoder.unconsumed_tail or self.decoder.unused_data:
			raise ValueError("fixture inflate bound exceeded")
		return result


def send_message(connection, opcode, payload, codec, masked):
	data = codec.encode(payload)
	if len(data) > 1:
		middle = len(data) // 2
		frame(connection, opcode, data[:middle], masked, codec.enabled, False)
		frame(connection, 9, b"between-fragments", masked)
		frame(connection, 0, data[middle:], masked)
	else:
		frame(connection, opcode, data, masked, codec.enabled)


class Peer(socketserver.BaseRequestHandler):
	def handle(self):
		self.request.settimeout(8)
		try:
			request, headers = read_head(self.request)
			target = request.split()[1].decode("ascii")
			parts = target.split("/")
			bits, flags = (int(parts[2]), int(parts[3])) if parts[1] == "echo" else (15, 0)
			offered = b"sec-websocket-extensions" in headers
			enabled = offered and target != "/decline"
			extension = f"permessage-deflate; server_max_window_bits={bits}; client_max_window_bits={bits}"
			if flags & 1:
				extension += "; client_no_context_takeover"
			if flags & 2:
				extension += "; server_no_context_takeover"
			if target == "/quote":
				extension = 'permessage-deflate; server_max_window_bits="15"; client_max_window_bits="15"'
			bad = {
				"unknown": "permessage-deflate; other=1",
				"duplicate": "permessage-deflate; client_max_window_bits=15; client_max_window_bits=15",
				"missing": "permessage-deflate; client_max_window_bits",
				"window8": "permessage-deflate; client_max_window_bits=8",
				"window16": "permessage-deflate; server_max_window_bits=16",
				"empty": "",
				"trailing": "permessage-deflate; server_max_window_bits=15 trailing",
				"extensions": "permessage-deflate, permessage-deflate",
				"headers": "permessage-deflate\r\nSec-WebSocket-Extensions: permessage-deflate",
				"flags": 'permessage-deflate; server_no_context_takeover=""',
				"unsolicited": "permessage-deflate",
			}
			if target.startswith("/bad/"):
				extension = bad[parts[2]]
				enabled = True
			accept = base64.b64encode(hashlib.sha1(headers[b"sec-websocket-key"] + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
			response = b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n"
			if enabled:
				response += b"Sec-WebSocket-Extensions: " + extension.encode("ascii") + b"\r\n"
			self.request.sendall(response + b"\r\n")
			if target.startswith("/bad/"):
				return
			codec = Codec(enabled, bits, bits, bool(flags & 2), bool(flags & 1))
			if target in ("/inflate-limit", "/invalid-deflate"):
				data = codec.encode(b"z" * 4097) if target == "/inflate-limit" else b"\xff"
				frame(self.request, 2, data, compressed=True)
				opcode, data, _ = read_message(self.request, True)
				if opcode != 8:
					raise ValueError("invalid compressed input was not closed")
				frame(self.request, 8, data)
				return
			while True:
				opcode, payload, compressed = read_message(self.request, True)
				if opcode == 8:
					frame(self.request, 8, payload)
					if isinstance(self.request, ssl.SSLSocket):
						self.request.unwrap().close()
					return
				data = codec.decode(payload, compressed)
				if opcode == 1:
					data.decode("utf-8")
				send_message(self.request, opcode, data, codec, False)
		except (OSError, ValueError, KeyError, ConnectionError, zlib.error) as error:
			# Invalid handshake cases and cancellation intentionally close the peer.
			if os.environ.get("VEROSCRIPT_TRACE_WS_COMPRESSION"):
				print("compression peer:", locals().get("target", "handshake"), error, file=sys.stderr, flush=True)
			return


class Listener(socketserver.ThreadingTCPServer):
	allow_reuse_address = True
	daemon_threads = True


def probe_server(executable, fixtures, bits, flags, host, enabled, fragments, offer=None, tls=False, expected=None):
	command = [executable, "--server", str(bits), str(flags), str(int(host)), str(int(enabled)), str(int(fragments))]
	if tls:
		command += [str(fixtures / "loopback-cert.pem"), str(fixtures / "loopback-key.pem")]
	process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
	try:
		port = process.stdout.readline().strip()
		if not port.isdecimal():
			raise AssertionError("native listener did not publish a port: " + port)
		with socket.create_connection(("127.0.0.1", int(port)), timeout=8) as connection:
			connection.settimeout(8)
			if tls:
				connection = ssl.create_default_context(cafile=fixtures / "loopback-cert.pem").wrap_socket(connection, server_hostname="localhost")
			if offer is None:
				offer = "permessage-deflate; client_max_window_bits=15; server_max_window_bits=15"
			key = b"dGhlIHNhbXBsZSBub25jZQ=="
			request = b"GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " + key + b"\r\n"
			if offer:
				request += b"Sec-WebSocket-Extensions: " + offer.encode("ascii") + b"\r\n"
			connection.sendall(request + b"\r\n")
			status, headers = read_head(connection)
			if not status.startswith(b"HTTP/1.1 101"):
				raise AssertionError(status)
			selected = headers.get(b"sec-websocket-extensions", b"").decode("ascii")
			compressed = bool(selected)
			parameters = {}
			if compressed:
				for parameter in selected.split(";")[1:]:
					name, _, value = parameter.strip().partition("=")
					if name in parameters:
						raise AssertionError("duplicate selected parameter")
					parameters[name] = value
			client_bits = int(parameters.get("client_max_window_bits", "15"))
			server_bits = int(parameters.get("server_max_window_bits", "15"))
			client_fresh = "client_no_context_takeover" in parameters
			server_fresh = "server_no_context_takeover" in parameters
			if compressed != (enabled if expected is None else expected):
				raise AssertionError((offer, selected))
			info = process.stdout.readline().strip().split()
			wanted = ["INFO", str(int(compressed)), str(client_bits if compressed else 0), str(server_bits if compressed else 0), str(int(client_fresh)), str(int(server_fresh))]
			if info != wanted:
				raise AssertionError((info, wanted, selected))
			codec = Codec(compressed, client_bits, server_bits, client_fresh, server_fresh)
			text = "中文-json-websocket\0".encode("utf-8") * 2000
			for opcode, payload in ((1, text), (1, text), (2, bytes(range(256)) * 256), (1, b"")):
				send_message(connection, opcode, payload, codec, True)
				returned_opcode, data, zipped = read_message(connection, False)
				if returned_opcode != opcode or codec.decode(data, zipped) != payload:
					raise AssertionError("native server changed compressed message")
			opcode, payload, zipped = read_message(connection, False)
			if opcode != 8 or zipped:
				raise AssertionError("native server did not close")
			frame(connection, 8, payload, True)
			if tls:
				try:
					connection.unwrap().close()
				except OSError:
					# The WebSocket close exchange is complete; the native transport may already be closed.
					connection.close()
		out, error = process.communicate(timeout=10)
		if process.returncode:
			raise AssertionError(f"native server failed ({process.returncode}): {out} {error}")
	finally:
		if process.poll() is None:
			process.kill()
			process.communicate()


def main():
	executable = str(Path(sys.argv[1]).resolve())
	fixtures = Path(__file__).resolve().parents[3] / "VeroScriptNetwork/Resources/Tests/Fixtures"
	enabled, native_tls = (value == "1" for value in subprocess.check_output([executable, "--capabilities"], text=True, timeout=10).split())
	with Listener(("127.0.0.1", 0), Peer) as plain, Listener(("127.0.0.1", 0), Peer) as secure:
		tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
		tls.load_cert_chain(fixtures / "loopback-cert.pem", fixtures / "loopback-key.pem")
		secure.socket = tls.wrap_socket(secure.socket, server_side=True)
		threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (plain, secure)]
		for thread in threads:
			thread.start()
		try:
			subprocess.run([executable, "--client", str(plain.server_address[1]), str(secure.server_address[1]), str(fixtures / "loopback-cert.pem")], timeout=100, check=True)
		finally:
			plain.shutdown()
			secure.shutdown()
			for thread in threads:
				thread.join()
	for host in (False, True):
		if enabled:
			for bits in (9, 12, 15):
				for flags in range(4):
					probe_server(executable, fixtures, bits, flags, host, True, bool(flags & 1))
			for offer in ("permessage-deflate; server_max_window_bits=8", "permessage-deflate; other=1", "permessage-deflate; client_max_window_bits=15; client_max_window_bits=15"):
				probe_server(executable, fixtures, 15, 0, host, True, False, offer, expected=False)
			probe_server(executable, fixtures, 15, 0, host, True, True,
						 "permessage-deflate; server_max_window_bits=8, permessage-deflate; client_max_window_bits; server_max_window_bits=12", expected=True)
			if native_tls:
				probe_server(executable, fixtures, 15, 3, host, True, True, tls=True)
		probe_server(executable, fixtures, 15, 0, host, False, False, expected=False)
	print("Independent Python zlib / native WebSocket compression matrix passed")


if __name__ == "__main__":
	main()
