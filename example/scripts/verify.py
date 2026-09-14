#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

"""Serve a local HTTP fixture or verify every public request example with uv."""

import argparse
from collections import Counter
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import time
import tomllib
from urllib.parse import parse_qs, urlsplit


EXAMPLE_ROOT = Path(__file__).resolve().parents[1]
BINARY_BODY = bytes(range(256)) + b"\x00\r\nmcr\xff"
FILE_DOWNLOADS = {"download", "download_async", "download_coro"}


def require(condition, message):
    """Report a verification failure even when Python assertions are disabled."""
    if not condition:
        raise AssertionError(message)


@dataclass(frozen=True)
class Example:
    """Describe one executable and the public API visibly called in its source."""

    name: str
    api: str
    method: str
    batch: bool
    download: bool
    json_body: bool


def discover_examples():
    """Match executable sources against all exported request declarations, including overloads."""
    api_source = (EXAMPLE_ROOT.parent / "src/api.cppm").read_text(encoding="utf-8")
    exported = api_source.split("export namespace mcr {", 1)[1]
    declarations = Counter(re.findall(r"^\s+(?:\[\[nodiscard\]\]\s+)?auto\s+(\w+)\(", exported, re.MULTILINE))
    manifest = tomllib.loads((EXAMPLE_ROOT / "mcpp.toml").read_text(encoding="utf-8"))
    covered = Counter()
    examples = []
    for name, target in manifest["targets"].items():
        require(target["kind"] == "bin", f"{name}: expected an executable")
        source_path = EXAMPLE_ROOT / target["main"]
        require(source_path.stem == name, f"{name}: filename must match the target")
        source = source_path.read_text(encoding="utf-8")
        calls = [call for call in re.findall(r"mcr::(\w+)\s*\(", source) if call in declarations]
        require(len(calls) == 1, f"{name}: expected exactly one direct public request call")
        api = calls[0]
        covered[api] += 1
        download = api.startswith("Download")
        batch = api.startswith("Multi")
        method = "GET" if download else re.sub(r"Async$|Callback$|Coro$", "", api.removeprefix("Multi")).upper()
        if name == "download":
            require(re.search(r"mcr::Download\(file\s*,", source), "Missing stream download example")
        elif name == "download_callback":
            require(re.search(r"mcr::Download\(write\s*,", source), "Missing WriteCallback download example")
        examples.append(Example(name, api, method, batch, download, "mcr::JsonBody" in source))
    require(not declarations - covered, f"API coverage differs: missing {declarations - covered}")
    json_apis = Counter(example.api for example in examples if example.json_body)
    expected_json_apis = Counter({name: 1 for name in (
        "Post", "PostAsync", "PostCoro", "PostCallback", "MultiPost", "MultiPostAsync",
    )})
    require(json_apis == expected_json_apis, f"JSON request modes differ: missing {expected_json_apis - json_apis}; extra {json_apis - expected_json_apis}")
    sources = {path.resolve() for path in (EXAMPLE_ROOT / "src").rglob("*.cpp")}
    entries = {(EXAMPLE_ROOT / target["main"]).resolve() for target in manifest["targets"].values()}
    require(sources == entries, "Some example sources have no corresponding executable target")
    return examples


class Fixture(ThreadingHTTPServer):
    """Record real requests while allowing concurrent batch transfers."""

    def __init__(self, port):
        self.records = []
        self.record_lock = threading.Lock()
        super().__init__(("127.0.0.1", port), Handler)

    def take_records(self):
        """Consume the requests from the last completed example."""
        with self.record_lock:
            records, self.records = self.records, []
        return records


class Handler(BaseHTTPRequestHandler):
    """Echo methods and bodies, supply binary data, and generate controlled HTTP failures."""

    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        """Keep successful fixture traffic out of the verification output."""

    def handle_request(self):
        """Handle the same endpoints for each supported HTTP method."""
        self.connection.settimeout(10)
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        request_id = query.get("request", ["single"])[0]
        with self.server.record_lock:
            self.server.records.append({
                "method": self.command,
                "query": query,
                "headers": dict(self.headers.items()),
                "body": body,
            })
        if request_id == "first":
            time.sleep(0.05)
        status = 200 if url.path in {"/echo", "/binary", "/json"} else 404
        content_type = "text/plain"
        if status == 404:
            output = b"not found"
        elif url.path == "/binary":
            output = BINARY_BODY
            content_type = "application/octet-stream"
        elif url.path == "/json":
            content_type = "application/json"
            try:
                document = json.loads(body)
            except (json.JSONDecodeError, UnicodeDecodeError):
                status = 400
                document = {"error": "invalid JSON request"}
            output = json.dumps(document, ensure_ascii=False).encode("utf-8")
        else:
            output = f"{self.command} {request_id} ".encode() + body
        self.send_response(status)
        self.send_header("Content-Length", str(len(output)))
        self.send_header("Content-Type", content_type)
        self.send_header("X-Method", self.command)
        self.send_header("X-Request-Id", request_id)
        self.send_header("Connection", "close")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(output)

    do_GET = handle_request
    do_POST = handle_request
    do_PUT = handle_request
    do_HEAD = handle_request
    do_DELETE = handle_request
    do_OPTIONS = handle_request
    do_PATCH = handle_request


def execute(binary, arguments, expected_exit, environment):
    """Execute a real example with a bounded runtime and capture its diagnostics."""
    result = subprocess.run(
        [str(binary), *map(str, arguments)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=25,
        env=environment,
    )
    require(result.returncode == expected_exit,
            f"{binary.name} {arguments}: expected exit {expected_exit}, got {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def verify_http(example, binary, fixture, environment, output_path, status):
    """Check HTTP behavior, batch ordering, and exact downloaded bytes."""
    endpoint = "/status/404" if status == 404 else "/binary" if example.download else "/json" if example.json_body else "/echo"
    url = f"http://127.0.0.1:{fixture.server_port}{endpoint}?seed=value"
    arguments = [url, output_path] if example.name in FILE_DOWNLOADS else [url]
    output = execute(binary, arguments, 0 if status == 200 else 1, environment)
    records = fixture.take_records()
    count = 2 if example.batch else 1
    require(len(records) == count, f"{example.name}: expected {count} requests, got {len(records)}")
    require(output.count(f"Status code: {status}") == count, f"{example.name}: response status was not reported")
    require(output.count(f"X-Method: {example.method}") == count, f"{example.name}: response headers were not reported")
    for record in records:
        require(record["method"] == example.method, f"{example.name}: wrong HTTP method {record['method']}")
        require(record["query"].get("seed") == ["value"], f"{example.name}: existing query was lost")
        expected_body = f"hello from {example.method}".encode() if example.method in {"POST", "PUT", "PATCH"} else b""
        if example.json_body:
            expected_document = {"name": "Alice", "enabled": True, "message": "你好，JSON!"}
            if example.batch:
                expected_document["request"] = record["query"].get("request", [None])[0]
            require(json.loads(record["body"]) == expected_document, f"{example.name}: wrong JSON request document")
        else:
            require(record["body"] == expected_body, f"{example.name}: wrong request body")
        if not example.download:
            require(record["headers"].get("User-Agent") == "mcr_example", f"{example.name}: missing User-Agent")
            if not example.batch:
                require(record["query"].get("message") == ["hello world"], f"{example.name}: query encoding failed")
        if example.json_body:
            require(record["headers"].get("Content-Type") == "application/json", f"{example.name}: missing automatic JSON media type")
        elif expected_body:
            require(record["headers"].get("Content-Type") == "text/plain", f"{example.name}: wrong request media type")
    if example.batch:
        require(sorted(record["query"].get("request") for record in records) == [["first"], ["second"]],
                f"{example.name}: batch requests were not distinct")
        require(re.findall(r"X-Request-Id: (\w+)", output) == ["first", "second"],
                f"{example.name}: batch results were not printed in argument order")
    if example.download:
        expected_bytes = BINARY_BODY if status == 200 else b"not found"
        require(f"Downloaded bytes: {len(expected_bytes)}" in output, f"{example.name}: incorrect byte count")
        if example.name in FILE_DOWNLOADS:
            require(output_path.read_bytes() == expected_bytes, f"{example.name}: binary file contents differ")
        else:
            require(f"Content (hex): {expected_bytes.hex()}" in output, "WriteCallback corrupted binary data")
    elif example.method == "HEAD":
        require(output.count("Text: \n") == count, f"{example.name}: HEAD returned body bytes")
    elif status == 404:
        require(output.count("Text: not found") == count, f"{example.name}: HTTP failure body was lost")
        if example.json_body:
            require("JSON parse failed:" not in output, f"{example.name}: HTTP failure was misreported as a JSON failure")
    elif example.json_body:
        require("Content-Type: application/json" in output, f"{example.name}: wrong response media type")
        documents = re.findall(r"^JSON: (.+)$", output, re.MULTILINE)
        require(len(documents) == count, f"{example.name}: parsed JSON was not printed")
        ordered_records = sorted(records, key=lambda record: record["query"].get("request", ["single"])[0])
        require([json.loads(document) for document in documents] == [json.loads(record["body"]) for record in ordered_records],
                f"{example.name}: parsed JSON responses must match every request in input order")
    else:
        for record in records:
            request_id = record["query"].get("request", ["single"])[0]
            echoed = f"Text: {example.method} {request_id} {record['body'].decode()}"
            require(echoed in output, f"{example.name}: response body was not printed")


def verify(bin_dir):
    """Run all entry points against an ephemeral loopback server without external services."""
    examples = discover_examples()
    environment = os.environ.copy()
    environment.update({"NO_PROXY": "127.0.0.1", "no_proxy": "127.0.0.1"})
    suffix = ".exe" if os.name == "nt" else ""
    with Fixture(0) as fixture, tempfile.TemporaryDirectory(prefix="mcr-examples-") as temporary:
        server_thread = threading.Thread(target=fixture.serve_forever, daemon=True)
        server_thread.start()
        try:
            for example in examples:
                binary = (bin_dir / (example.name + suffix)).resolve()
                require(binary.is_file(), f"Build the missing example: {binary}")
                output_path = Path(temporary) / f"{example.name}.bin"
                require("Usage:" in execute(binary, ["--help"], 0, environment), f"{example.name}: missing help")
                require("Usage:" in execute(binary, [], 2, environment), f"{example.name}: missing usage")
                extra_arguments = ["unused"] * (3 if example.name in FILE_DOWNLOADS else 2)
                execute(binary, extra_arguments, 2, environment)
                verify_http(example, binary, fixture, environment, output_path, 200)
                verify_http(example, binary, fixture, environment, output_path, 404)
                if example.json_body:
                    url = f"http://127.0.0.1:{fixture.server_port}/echo"
                    output = execute(binary, [url], 1, environment)
                    count = 2 if example.batch else 1
                    require(output.count("Status code: 200") == count and output.count("JSON parse failed:") == count,
                            f"{example.name}: invalid JSON must fail despite a successful HTTP status")
                    require(len(fixture.take_records()) == count, f"{example.name}: expected {count} invalid JSON responses")
                invalid_url = "unsupported-scheme://example"
                arguments = [invalid_url, output_path] if example.name in FILE_DOWNLOADS else [invalid_url]
                require("Request failed:" in execute(binary, arguments, 1, environment), f"{example.name}: transport failure was not reported")
                if example.name in FILE_DOWNLOADS:
                    bad_output = Path(temporary) / "missing-directory" / "output.bin"
                    url = f"http://127.0.0.1:{fixture.server_port}/binary"
                    require("Example failed:" in execute(binary, [url, bad_output], 1, environment),
                            f"{example.name}: file opening failure was not reported")
                require(not fixture.take_records(), f"{example.name}: argument or file failures unexpectedly sent a request")
                print(f"{example.name}: ok", flush=True)
        finally:
            fixture.shutdown()
            server_thread.join(timeout=5)
    print(f"{len(examples)} request examples passed; every public request declaration is covered.")


def main():
    """Select interactive fixture serving or executable verification."""
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--serve", action="store_true", help="serve local HTTP endpoints until Ctrl+C")
    mode.add_argument("--bin-dir", type=Path, help="directory containing the built example executables")
    parser.add_argument("--port", type=int, default=8080, help="loopback port used with --serve (default: 8080)")
    args = parser.parse_args()
    if args.serve:
        with Fixture(args.port) as fixture:
            print(f"Serving http://127.0.0.1:{fixture.server_port}/echo, /json and /binary (Ctrl+C to stop)", flush=True)
            try:
                fixture.serve_forever()
            except KeyboardInterrupt:
                pass
    else:
        verify(args.bin_dir)


if __name__ == "__main__":
    main()
