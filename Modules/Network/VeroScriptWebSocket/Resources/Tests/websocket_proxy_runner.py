# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

"""Independent HTTP CONNECT and ws/wss peers; no external service is used."""
import base64
import hashlib
from pathlib import Path
import select
import socket
import socketserver
import ssl
import struct
import subprocess
import sys
import threading


def read_head(connection):
	data = bytearray()
	# Read exactly through the head so a following TLS record is never lost.
	while not data.endswith(b"\r\n\r\n"):
		chunk = connection.recv(1)
		if not chunk or len(data) >= 65536:
			raise ConnectionError("incomplete head")
		data.extend(chunk)
	lines = bytes(data).split(b"\r\n")
	headers = {}
	for line in lines[1:]:
		if line:
			key, value = line.split(b":", 1)
			headers[key.strip().lower()] = value.strip()
	return lines[0], headers


def exact(connection, count):
	data = bytearray()
	while len(data) < count:
		chunk = connection.recv(count - len(data))
		if not chunk:
			raise ConnectionError("incomplete frame")
		data.extend(chunk)
	return bytes(data)


class Peer(socketserver.BaseRequestHandler):
	attempts = {}
	lock = threading.Lock()

	def handle(self):
		self.request.settimeout(5)
		try:
			request, headers = read_head(self.request)
			method, target, version = request.split()
			if b"proxy-authorization" in headers or method != b"GET" or version != b"HTTP/1.1":
				self.request.sendall(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
				return
			if target.startswith(b"/retry/"):
				failures = int(target.rsplit(b"/", 1)[1])
				with self.lock:
					attempt = self.attempts.get(target, 0) + 1
					self.attempts[target] = attempt
				if attempt <= failures:
					self.request.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n")
					if isinstance(self.request, ssl.SSLSocket):
						self.request.unwrap().close()
					return
			elif target != b"/echo":
				self.request.sendall(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
				return
			key = headers[b"sec-websocket-key"] + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
			accept = base64.b64encode(hashlib.sha1(key).digest())
			self.request.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
								 b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
			while True:
				header = exact(self.request, 2)
				count = header[1] & 127
				if count == 126:
					count = struct.unpack("!H", exact(self.request, 2))[0]
				elif count == 127:
					count = struct.unpack("!Q", exact(self.request, 8))[0]
				if count > 65536 or not header[1] & 128:
					raise ConnectionError("invalid client frame")
				mask = exact(self.request, 4)
				data = exact(self.request, count)
				data = bytes(value ^ mask[index % 4] for index, value in enumerate(data))
				opcode = header[0] & 15
				response_opcode = 10 if opcode == 9 else opcode
				framing = bytes([0x80 | response_opcode, count]) if count < 126 else bytes([0x80 | response_opcode, 126]) + struct.pack("!H", count)
				self.request.sendall(framing + data)
				if opcode == 8:
					if isinstance(self.request, ssl.SSLSocket):
						self.request.unwrap().close()
					return
		except (OSError, ConnectionError, ValueError, KeyError):
			pass


class Proxy(socketserver.BaseRequestHandler):
	def handle(self):
		self.request.settimeout(5)
		try:
			request, headers = read_head(self.request)
			method, target, version = request.split()
			if method != b"CONNECT" or version != b"HTTP/1.1" or headers.get(b"host") != target:
				raise ConnectionError("invalid CONNECT")
			auth = headers.get(b"proxy-authorization", b"")
			if auth == b"Test stall":
				while self.request.recv(1):
					pass
				return
			if auth != b"Basic dXNlcjpwYXNz":
				self.request.sendall(b"HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n")
				return
			host, port = target.decode("ascii").rsplit(":", 1)
			if host != "127.0.0.1":
				raise ConnectionError("unexpected target")
			with socket.create_connection((host, int(port)), timeout=5) as upstream:
				# A successful CONNECT ignores response Content-Length (RFC 9110).
				self.request.sendall(b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 Connection Established\r\nContent-Length: 999\r\nTransfer-Encoding: chunked\r\n\r\n")
				while True:
					ready, _, _ = select.select([self.request, upstream], [], [], 5)
					if not ready:
						return
					for source in ready:
						data = source.recv(65536)
						if not data:
							return
						(upstream if source is self.request else self.request).sendall(data)
		except (OSError, ConnectionError, ValueError):
			pass


class Server(socketserver.ThreadingTCPServer):
	daemon_threads = True
	allow_reuse_address = True


def main():
	fixtures = Path(__file__).resolve().parents[3] / "VeroScriptNetwork/Resources/Tests/Fixtures"
	with Server(("127.0.0.1", 0), Proxy) as proxy, Server(("127.0.0.1", 0), Peer) as plain, Server(("127.0.0.1", 0), Peer) as secure:
		tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
		tls.load_cert_chain(fixtures / "loopback-cert.pem", fixtures / "loopback-key.pem")
		secure.socket = tls.wrap_socket(secure.socket, server_side=True)
		servers = (proxy, plain, secure)
		threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in servers]
		for thread in threads:
			thread.start()
		try:
			return subprocess.run([sys.argv[1], *(str(server.server_address[1]) for server in servers),
								   str(fixtures / "loopback-cert.pem"), *sys.argv[2:]], timeout=60).returncode
		finally:
			for server in servers:
				server.shutdown()
			for thread in threads:
				thread.join()


if __name__ == "__main__":
	sys.exit(main())
