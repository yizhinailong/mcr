"""Generate ephemeral TLS credentials and serve ordinary HTTPS and mutual TLS locally."""

import argparse
import base64
import hashlib
import http.server
import pathlib
import ssl
import subprocess
import threading
import time


def generate_certificates(directory: pathlib.Path, openssl: str) -> None:
    """Create an isolated CA, server certificate, and client certificate without changing trust stores."""

    def run(*arguments: str) -> bytes:
        """Capture command failures in the fixture log, including the OpenSSL diagnostic."""
        result = subprocess.run(
            [openssl, *arguments], cwd=directory, capture_output=True, check=False
        )
        if result.returncode:
            raise RuntimeError(result.stderr.decode(errors="replace"))
        return result.stdout

    run(
        "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256", "-days", "1",
        "-subj", "/CN=mcr-test-root", "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        "-keyout", "ca-key.pem", "-out", "ca.pem",
    )
    for name, common_name, purpose in (
        ("server", "localhost", "serverAuth"),
        ("client", "mcr-client", "clientAuth"),
    ):
        extension = (
            "basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature,keyEncipherment\n"
            f"extendedKeyUsage={purpose}\n"
        )
        if name == "server":
            extension += "subjectAltName=DNS:localhost\n"
        (directory / f"{name}.ext").write_text(extension, encoding="ascii")
        run(
            "req", "-new", "-newkey", "rsa:2048", "-nodes", "-sha256",
            "-subj", f"/CN={common_name}", "-keyout", f"{name}-key.pem",
            "-out", f"{name}.csr",
        )
        run(
            "x509", "-req", "-in", f"{name}.csr", "-CA", "ca.pem",
            "-CAkey", "ca-key.pem", "-CAcreateserial", "-days", "1", "-sha256",
            "-extfile", f"{name}.ext", "-out", f"{name}.pem",
        )
    public_key = run("pkey", "-in", "server-key.pem", "-pubout", "-outform", "DER")
    pin = base64.b64encode(hashlib.sha256(public_key).digest()).decode("ascii")
    (directory / "pin.txt").write_text(f"sha256//{pin}", encoding="ascii")


class Handler(http.server.BaseHTTPRequestHandler):
    """Return one short response and close the connection so each request verifies its TLS options."""

    def do_GET(self) -> None:
        """Send a deterministic response after a successful handshake."""
        body = b"TLS works"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_arguments) -> None:
        """Suppress successful request logs while retaining uncaught server diagnostics."""


class HttpsServer(http.server.ThreadingHTTPServer):
    """Wrap each accepted socket with an isolated TLS context and a bounded handshake."""

    daemon_threads = True

    def __init__(self, directory: pathlib.Path, mutual: bool):
        """Require TLS 1.2 and optionally a client signed by this fixture's CA."""
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.maximum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(directory / "server.pem", directory / "server-key.pem")
        if mutual:
            self.context.load_verify_locations(cafile=directory / "ca.pem")
            self.context.verify_mode = ssl.CERT_REQUIRED
        super().__init__(("127.0.0.1", 0), Handler)

    def get_request(self):
        """Close rejected sockets; failed verification is expected in negative test cases."""
        connection, address = super().get_request()
        connection.settimeout(3)
        try:
            return self.context.wrap_socket(connection, server_side=True), address
        except BaseException:
            connection.close()
            raise


def main() -> None:
    """Publish both ports after generating credentials and stop when the parent requests cleanup."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=pathlib.Path, required=True)
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    generate_certificates(args.directory, args.openssl)
    servers = [HttpsServer(args.directory, mutual) for mutual in (False, True)]
    threads = [threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}) for server in servers]
    try:
        for thread in threads:
            thread.start()
        (args.directory / "ready").write_text(
            " ".join(str(server.server_port) for server in servers), encoding="ascii"
        )
        while not (args.directory / "stop").exists():
            time.sleep(0.025)
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()
        for thread in threads:
            thread.join()


if __name__ == "__main__":
    main()
