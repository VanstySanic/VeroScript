# SPDX-FileCopyrightText: VanstySanic
# SPDX-License-Identifier: MIT

import http.server
import socketserver
import subprocess
import sys
import threading
import ssl
import base64
import hashlib
import os
from email import policy
from email.parser import BytesParser
from pathlib import Path


class HttpHandler(http.server.BaseHTTPRequestHandler):
	protocol_version = "HTTP/1.1"
	policy_lock = threading.Lock()
	policy_counts = {}

	def log_message(self, *args):
		if os.environ.get("VEROSCRIPT_TRACE_HTTP_POLICY"):
			print(self.command, self.path, *args, flush=True)

	def handle(self):
		try:
			super().handle()
		except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
			# Cancellation and intentionally abandoned responses close the peer.
			pass

	def client_policy(self, body=b""):
		if self.path == "/cookie-echo":
			data=self.headers.get("Cookie", "").encode("utf-8")
		elif self.path.split("?", 1)[0] == "/method-echo":
			data=self.command.encode("ascii")+b":"+body
		elif self.path.startswith("/trace/"):
			if self.path == "/trace/chain/start":
				self.send_response_only(103)
				self.send_header("Link", "</style.css>; rel=preload")
				self.end_headers()
				status, locations = 302, ["next?from=start"]
			elif self.path == "/trace/chain/next?from=start":
				status, locations = 307, ["../../method-echo?done"]
			elif self.path == "/trace/loop":
				status, locations = 302, ["/trace/loop"]
			elif self.path == "/trace/failed-target":
				closed_port = self.headers.get("X-Test-Closed-Port", "1")
				status, locations = 307, [f"http://127.0.0.1:{closed_port}/not-listening"]
			else:
				status, locations = 302, ["/method-echo", "/method-echo"]
			self.send_response(status)
			for location in locations:
				self.send_header("Location", location)
			self.send_header("Content-Length", "0")
			self.end_headers()
			return True
		elif self.path == "/empty-location":
			data=b"stay"
			self.send_response(302)
			self.send_header("Location", "")
			self.send_header("Content-Length",str(len(data)))
			self.end_headers()
			self.wfile.write(data)
			return True
		elif self.path in ("/redirect-303", "/redirect-307"):
			self.send_response(303 if self.path.endswith("303") else 307)
			self.send_header("Location", "/method-echo")
			self.send_header("Content-Length", "0")
			self.end_headers()
			return True
		elif self.path.startswith("/retry/"):
			failures=int(self.path.rsplit("/",1)[1])
			key=self.command+":"+self.path
			with self.policy_lock:
				count=self.policy_counts.get(key,0)+1
				self.policy_counts[key]=count
			data=str(count).encode("ascii")
			self.send_response(503 if count<=failures else 200)
			if count<=failures:
				self.send_header("Retry-After",self.headers.get("X-Test-Retry-After","0"))
			self.send_header("Content-Length",str(len(data)))
			self.end_headers()
			self.wfile.write(data)
			return True
		else:
			return False
		self.send_response(200)
		self.send_header("Content-Length",str(len(data)))
		self.end_headers()
		self.wfile.write(data)
		return True

	def do_GET(self):
		if self.client_policy():
			return
		if self.path.startswith('/download/'):
			data = bytes(i % 251 for i in range(40001))
			original_length = len(data)
			status = 200
			content_range = None
			etag = '"file-v1"'
			date = 'Sun, 06 Nov 1994 08:49:37 GMT'
			requested = self.headers.get('Range', '')
			if self.path in ('/download/range', '/download/bad-range', '/download/bad-validator', '/download/416-body') and requested:
				start = int(requested.removeprefix('bytes=').removesuffix('-'))
				if self.headers.get('If-Range') in (etag, date):
					if start >= len(data):
						status = 416
						content_range = f'bytes */{len(data)}'
						data = b'already complete' if self.path.endswith('416-body') else b''
					else:
						status = 206
						content_range = f'bytes {start + (1 if self.path.endswith("bad-range") else 0)}-{len(data)-1}/{len(data)}'
						data = data[start:]
			if self.path.endswith('bad-validator'):
				etag = '"file-v2"'
			if self.path.endswith('missing'):
				status, data = 404, b'missing'
			self.send_response(status)
			self.send_header('ETag', etag)
			self.send_header('Last-Modified', date)
			if content_range:
				self.send_header('Content-Range', content_range)
			if self.path.endswith('encoded'):
				self.send_header('Content-Encoding', 'gzip')
			self.send_header('Content-Length', str(original_length + 9 if self.path.endswith('truncated') else len(data)))
			self.end_headers()
			self.wfile.write(data)
			if self.path.endswith('truncated'):
				self.close_connection = True
			return
		if self.path.startswith('/sse-'):
			if self.path in ('/sse-large-default', '/sse-explicit-total-limit'):
				block = (b':' + b'x' * 126 + b'\n') * 512
				count = 1025 if self.path == '/sse-large-default' else 1
				event = b'data: after-64-mib\n\n'
				self.send_response(200)
				self.send_header('Content-Type', 'text/event-stream')
				self.send_header('Content-Length', str(len(block) * count + len(event)))
				self.end_headers()
				for _ in range(count):
					self.wfile.write(block)
				self.wfile.write(event)
				return
			last_id = self.headers.get('Last-Event-ID', '')
			if (self.path == '/sse-stop' or (self.path == '/sse-reconnect' and last_id == 'silent') or
					(self.path == '/sse-disconnect' and last_id == 'resume')):
				self.send_response(204)
				self.end_headers()
				return
			if self.path == '/sse-disconnect':
				data = b'id: resume\nretry: 0\ndata: before-close\n\n'
				self.send_response(200)
				self.send_header('Content-Type', 'text/event-stream')
				self.send_header('Content-Length', str(len(data) + 100))
				self.end_headers()
				self.wfile.write(data)
				self.wfile.flush()
				self.close_connection = True
				return
			if self.path == '/sse-stall':
				self.send_response(200)
				self.send_header('Content-Type', 'text/event-stream')
				self.send_header('Content-Length', '100')
				self.end_headers()
				self.wfile.flush()
				self.connection.recv(1)
				return
			if self.path == '/sse-reconnect':
				data = (b'data: two\n\nid: silent\n\n' if last_id == 'first' else
						b'\xef\xbb\xbfid: first\nretry: 1\ndata: one\n\n')
			elif self.path == '/sse-batch':
				data = b'data: x\n\n' * 64
			else:
				data = b'data: ' + b'x' * 256 + b'\n\n'
			self.send_response(200)
			self.send_header('Content-Type', 'TEXT/EVENT-STREAM; charset=utf-8')
			self.send_header('Content-Length', str(len(data)))
			self.end_headers()
			if self.path == '/sse-batch':
				self.wfile.write(data)
				return
			for start in range(0, len(data), 3):
				self.wfile.write(data[start:start + 3])
			return
		if self.path.startswith('/ws-protocol-'):
			self.send_response_only(101)
			self.send_header('Upgrade', 'websocket')
			self.send_header('Connection', 'Upgrade')
			key = self.headers.get('Sec-WebSocket-Key', '') + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
			self.send_header('Sec-WebSocket-Accept', base64.b64encode(hashlib.sha1(key.encode('ascii')).digest()).decode('ascii'))
			protocol = 'other' if self.path.endswith('unoffered') else 'game.v1'
			self.send_header('Sec-WebSocket-Protocol', protocol)
			if self.path.endswith('multiple'):
				self.send_header('Sec-WebSocket-Protocol', protocol)
			self.end_headers()
			self.wfile.write(b'\x81\x02ok')
			self.close_connection = True
			return
		if self.path == "/stream-redirect":
			self.send_response(302)
			self.send_header("Location", "/stream")
			self.send_header("Content-Length", "13")
			self.end_headers()
			self.wfile.write(b"redirect-body")
			return
		if self.path == "/stream-chunked":
			self.send_response(200)
			self.send_header("Transfer-Encoding", "chunked")
			self.send_header("Trailer", "X-Final")
			self.end_headers()
			for chunk in (b"first\x00", b"second"):
				self.wfile.write(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
			self.wfile.write(b"0\r\nX-Final: done\r\n\r\n")
			return
		if self.path in ("/stream", "/stream-stall"):
			count = 1024 * 1024 + 37
			self.send_response(200)
			self.send_header("Content-Length", str(count))
			self.end_headers()
			self.wfile.flush()
			if self.path == "/stream-stall":
				self.connection.recv(1)
				return
			try:
				for start in range(0, count, 8191):
					self.wfile.write(bytes(i % 251 for i in range(start, min(start + 8191, count))))
			except (BrokenPipeError, ConnectionResetError):
				pass
			return
		if self.path == "/stall":
			self.connection.recv(1)
			return
		if self.path == "/truncated":
			self.send_response(200)
			self.send_header("Content-Length", "100")
			self.end_headers()
			self.wfile.write(b"x")
			self.close_connection = True
			return
		data = b"ok"
		self.send_response(404 if self.path == "/404" else 200)
		self.send_header("Content-Length", str(len(data)))
		self.send_header("Set-Cookie", "a=1")
		self.send_header("Set-Cookie", "b=2")
		self.end_headers()
		self.wfile.write(data)

	def do_HEAD(self):
		self.send_response(200)
		self.send_header("Content-Length", "2")
		self.end_headers()

	def do_POST(self):
		if self.headers.get("Transfer-Encoding", "").lower() == "chunked":
			body = bytearray()
			while True:
				line = self.rfile.readline(128)
				if not line:
					break
				count = int(line.split(b";", 1)[0].strip(), 16)
				if not count:
					while self.rfile.readline(65536) not in (b"\r\n", b"\n", b""):
						pass
					break
				body.extend(self.rfile.read(count))
				if self.rfile.read(2) != b"\r\n":
					break
			data = bytes(body)
		else:
			data = self.rfile.read(int(self.headers["Content-Length"]))
		if self.path == "/multipart" or self.path.startswith("/retry/multipart-"):
			mime = BytesParser(policy=policy.default).parsebytes(
				b"Content-Type: " + self.headers["Content-Type"].encode("ascii") + b"\r\n\r\n" + data)
			actual = [(part.get_param("name", header="content-disposition"), part.get_filename(),
					   part.get_content_type(), part.get_payload(decode=True)) for part in mime.iter_parts()]
			expected = [("field", None, "text/plain", b"alpha\x00omega"),
						("field", None, "text/plain", b"second"),
						("file", "hello.bin", "application/octet-stream", b"\x00\xff")]
			if self.path.startswith("/retry/multipart-") and actual == expected:
				self.client_policy(data)
				return
			data = b"multipart-ok" if actual == expected else b"multipart-mismatch"
		elif self.client_policy(data):
			return
		self.send_response(200)
		self.send_header("Content-Length", str(len(data)))
		self.end_headers()
		self.wfile.write(data)

	do_PUT = do_POST


class TcpHandler(socketserver.BaseRequestHandler):
	def handle(self):
		while data := self.request.recv(65536):
			self.request.sendall(data)


class TcpServer(socketserver.ThreadingTCPServer):
	daemon_threads = True
	allow_reuse_address = True


class HttpServer(http.server.ThreadingHTTPServer):
	daemon_threads = True
	request_queue_size = 128


def main():
	fixtures = Path(__file__).resolve().parents[3] / "VeroScriptNetwork/Resources/Tests/Fixtures"
	with HttpServer(("127.0.0.1", 0), HttpHandler) as http, HttpServer(("127.0.0.1", 0), HttpHandler) as https, TcpServer(("127.0.0.1", 0), TcpHandler) as tcp:
		tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
		tls.load_cert_chain(fixtures / "loopback-cert.pem", fixtures / "loopback-key.pem")
		https.socket = tls.wrap_socket(https.socket, server_side=True)
		servers = (http, https, tcp)
		threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in servers]
		for thread in threads:
			thread.start()
		try:
			return subprocess.run([sys.argv[1], str(http.server_port), str(tcp.server_address[1]), str(https.server_port), str(fixtures / "loopback-cert.pem"), *sys.argv[2:]], timeout=75).returncode
		finally:
			for server in servers:
				server.shutdown()
			for thread in threads:
				thread.join()


if __name__ == "__main__":
	sys.exit(main())
