import socket
import struct
import os
import sys


class FtpClient:
    def __init__(self, host, port):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
        self.buffer = b""

    def close(self):
        self.sock.close()

    def _recv_exact(self, n):
        """Receive exactly n bytes, buffering as needed."""
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("Connection closed by server")
            self.buffer += chunk
        data = self.buffer[:n]
        self.buffer = self.buffer[n:]
        return data

    # --- send ---

    def _send_list(self):
        self.sock.sendall(b"\x01")

    def _send_download(self, filename):
        name_bytes = filename.encode("utf-8")
        self.sock.sendall(b"\x02" + struct.pack("<B", len(name_bytes)) + name_bytes)

    # --- recv ---

    def _recv_list(self):
        # uint16 count, then count * FileDesc
        count = struct.unpack("<H", self._recv_exact(2))[0]
        files = []
        for _ in range(count):
            header = self._recv_exact(9)  # uint64 size + uint8 name_len
            filesize = struct.unpack("<Q", header[:8])[0]
            name_len = header[8]
            filename = self._recv_exact(name_len).decode("utf-8", errors="replace")
            files.append((filename, filesize))
        return files

    def _recv_download(self):
        # bool success (1) + size_t filesize (8)
        header = self._recv_exact(9)
        success = bool(header[0])
        filesize = struct.unpack("<Q", header[1:9])[0]
        content = self._recv_exact(filesize) if filesize > 0 else b""
        return success, content

    # --- public api ---

    def list_files(self):
        self._send_list()
        return self._recv_list()

    def download_file(self, remote_name):
        self._send_download(remote_name)
        return self._recv_download()


def format_size(size):
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024:
            return f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} TB"


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "100.66.115.49"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 12345

    print(f"Connecting to {host}:{port}...")
    client = FtpClient(host, port)
    print("Connected!\n")

    # cache last listing so user can refer by index
    cached_files = []

    try:
        while True:
            print("Commands:  [l]ist   [d]ownload <file|#>   [a]ll   [q]uit")
            try:
                line = input("> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break

            if not line:
                continue

            parts = line.split(maxsplit=1)
            cmd = parts[0].lower()

            if cmd in ("l", "list"):
                cached_files = client.list_files()
                if not cached_files:
                    print("  (no files)\n")
                else:
                    print(f"  {'#':<4} {'Filename':<50} {'Size':>12}")
                    print(f"  {'─'*4} {'─'*50} {'─'*12}")
                    for i, (name, size) in enumerate(cached_files, 1):
                        print(f"  {i:<4} {name:<50} {format_size(size):>12}")
                    print()

            elif cmd in ("d", "download"):
                if len(parts) < 2:
                    print("  Usage: download <filename or #index>\n")
                    continue

                arg = parts[1]

                # allow picking by index from last listing
                if arg.isdigit() and cached_files:
                    idx = int(arg) - 1
                    if 0 <= idx < len(cached_files):
                        arg = cached_files[idx][0]
                    else:
                        print(f"  Index out of range (1-{len(cached_files)})\n")
                        continue

                print(f"  Downloading '{arg}'...")
                success, content = client.download_file(arg)
                if not success:
                    print("  Failed (file not found or access denied)\n")
                else:
                    local = os.path.basename(arg)
                    with open(local, "wb") as f:
                        f.write(content)
                    print(f"  Saved '{local}' ({format_size(len(content))})\n")

            elif cmd in ("a", "all"):
                # download every file from last listing
                if not cached_files:
                    print("  Run 'list' first\n")
                    continue
                for name, size in cached_files:
                    print(f"  Downloading '{name}'...", end=" ")
                    success, content = client.download_file(name)
                    if not success:
                        print("FAILED")
                    else:
                        local = os.path.basename(name)
                        with open(local, "wb") as f:
                            f.write(content)
                        print(f"OK ({format_size(len(content))})")
                print()

            elif cmd in ("q", "quit", "exit"):
                break

            else:
                print("  Unknown command\n")

    finally:
        client.close()
        print("Disconnected.")


if __name__ == "__main__":
    main()