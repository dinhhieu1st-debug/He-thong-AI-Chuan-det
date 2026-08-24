#!/usr/bin/env python3
"""Relay the gateway's newline-delimited local TCP stream to HIS over HTTPS."""

from __future__ import annotations

import http.client
import logging
import os
import socket
import socketserver
import ssl
import time
import urllib.parse


LISTEN_HOST = os.environ.get("BRIDGE_LISTEN_HOST", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("BRIDGE_LISTEN_PORT", "15000"))
INGEST_URL = os.environ.get(
    "HIS_INGEST_URL", "https://smartivcare.io.vn/api/ingestion/bed"
)
API_KEY = os.environ.get("HTTP_INGESTION_KEY", "")
GATEWAY_ID = os.environ.get("GATEWAY_ID", socket.gethostname())
MAX_LINE_BYTES = 64 * 1024

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(message)s",
)


class HisClient:
    def __init__(self) -> None:
        parsed = urllib.parse.urlsplit(INGEST_URL)
        if parsed.scheme != "https" or not parsed.hostname:
            raise RuntimeError("HIS_INGEST_URL must be an https URL")
        self.host = parsed.hostname
        self.port = parsed.port or 443
        self.path = urllib.parse.urlunsplit(
            ("", "", parsed.path or "/", parsed.query, "")
        )
        self.context = ssl.create_default_context()
        self.connection: http.client.HTTPSConnection | None = None

    def close(self) -> None:
        if self.connection is not None:
            self.connection.close()
            self.connection = None

    def post(self, payload: bytes) -> bytes:
        if self.connection is None:
            self.connection = http.client.HTTPSConnection(
                self.host,
                self.port,
                timeout=15,
                context=self.context,
            )
        try:
            self.connection.request(
                "POST",
                self.path,
                body=payload,
                headers={
                    "Content-Type": "application/json",
                    "Content-Length": str(len(payload)),
                    "X-Ingestion-Key": API_KEY,
                    "X-Gateway-Id": GATEWAY_ID,
                    "User-Agent": "smartiv-pi-http-bridge/1.0",
                },
            )
            response = self.connection.getresponse()
            response_body = response.read()
            if response.status not in (200, 202):
                raise RuntimeError(f"HIS returned HTTP {response.status}")
            if response.will_close:
                self.close()
            return response_body
        except Exception:
            self.close()
            raise


class GatewayHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        logging.info("Gateway connected from %s", self.client_address[0])
        client = HisClient()
        try:
            while True:
                line = self.rfile.readline(MAX_LINE_BYTES + 1)
                if not line:
                    break
                if len(line) > MAX_LINE_BYTES:
                    logging.error("Dropped oversized gateway payload")
                    continue

                payload = line.strip()
                if not payload:
                    continue

                for attempt in range(1, 6):
                    try:
                        commands = client.post(payload)
                        if commands:
                            self.wfile.write(commands)
                            self.wfile.flush()
                        break
                    except Exception as error:
                        if attempt == 5:
                            logging.error(
                                "Dropped payload after 5 HTTPS attempts: %s", error
                            )
                            break
                        delay = min(2 ** (attempt - 1), 8)
                        logging.warning(
                            "HTTPS attempt %s failed: %s; retrying in %ss",
                            attempt,
                            error,
                            delay,
                        )
                        time.sleep(delay)
        finally:
            client.close()
        logging.info("Gateway disconnected")


class BridgeServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> None:
    if not API_KEY:
        raise SystemExit("HTTP_INGESTION_KEY is required")
    with BridgeServer((LISTEN_HOST, LISTEN_PORT), GatewayHandler) as server:
        logging.info(
            "TCP-to-HTTPS bridge listening on %s:%s -> %s",
            LISTEN_HOST,
            LISTEN_PORT,
            INGEST_URL,
        )
        server.serve_forever()


if __name__ == "__main__":
    main()
