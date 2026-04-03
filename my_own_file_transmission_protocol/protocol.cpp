#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cassert>
#include <csignal>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define error(msg)                                                            \
  {                                                                           \
    std::cerr << "error at line : " << __LINE__ << " : " << msg << std::endl; \
    std::exit(-1);                                                            \
  }

#define debug(msg)                          \
  {                                         \
    std::cerr << "[" << __LINE__ << "]"     \
              << " : " << msg << std::endl; \
  }

namespace MyFtp {

struct FileDesc {
  static constexpr std::uint64_t namesize = 256;
  std::uint64_t size;
  std::uint8_t filename_length;
  char filename[namesize];

  size_t serialize_to(std::byte* ptr)
      const {  // ptr must have enough size , passing nullptr for realsize
    if (ptr) {
      ptr = (std::byte*)memcpy(ptr, &size, sizeof(size));
      ptr += sizeof(size);
      ptr = (std::byte*)memcpy(ptr, &filename_length, sizeof(filename_length));
      ptr += sizeof(filename_length);
      ptr = (std::byte*)memcpy(ptr, &filename, filename_length);
    }
    return sizeof(size) + sizeof(filename_length) + filename_length;
  }
};

struct ListRes {
  std::vector<FileDesc> files;

  size_t serialize_to(std::byte* ptr) const {
    size_t total = sizeof(std::uint16_t);
    std::uint16_t ss = files.size();
    if (ptr) {
      ptr = (std::byte*)memcpy(ptr, &ss, sizeof(ss));
      ptr += sizeof(ss);
    }
    for (const auto& desc : files) {
      if (ptr) {
        ptr += desc.serialize_to(ptr);
      }
      total += desc.serialize_to(nullptr);
    }

    return total;
  }
};

struct __attribute__((packed)) FileDescHeader {
  static constexpr std::uint64_t namesize = 256;
  std::uint64_t size;
  std::uint8_t filename_length;
};

struct __attribute__((packed)) ListResHeader {
  std::uint16_t dir_size;
};

enum class CommandEnum : uint8_t { list = 1 };

struct __attribute__((packed)) Command {
  CommandEnum command_enum;
};

class CircularBuffer {
 public:
  // read atmost n byte from circular buffer and handle the position advancing
  size_t read(std::byte* rec, size_t n) {
    size_t count = 0;
    while (n && tail > head) {
      *(rec++) = buf[head % bufsize];
      head++;
      n--;
      count++;
    }
    return count;
  }

  int must_read(
      std::byte* rec,
      size_t
          n) {  // must read exactly n bytes , return 0 upon success , else -1
    if ((tail - head) < n)
      return -1;
    read(rec, n);
    return 0;
  }

  size_t push(const std::byte* rec, size_t n) {
    size_t count = 0;
    while (n && (tail - head) < bufsize) {
      buf[(tail++) % bufsize] = *(rec++);
      n--;
      count++;
    }
    return count;
  }

 private:
  static constexpr size_t bufsize = 1 << 12;  // is enough
  std::byte buf[bufsize];
  size_t head = 0;
  size_t tail = 0;
};

// parsing incoming tcp stream into command
class TcpMsgParser {
 public:
  void exahust() {
    std::byte to_read[sizeof(Command)];
    while (cbuf.must_read(to_read, sizeof(Command)) == 0) {
      commands.push_back(*reinterpret_cast<Command*>(&to_read));
    }
  }

  void receive(const std::byte* buf, size_t len) {
    exahust();
    assert(cbuf.push(buf, len) == len);
  }

  std::optional<Command> pop() {
    exahust();
    if (commands.size()) {
      auto res = commands.front();
      commands.pop_front();
      return res;
    }
    return std::nullopt;
  }

 private:
  std::deque<Command> commands;
  CircularBuffer cbuf;
};

struct Context {

  Context(sockaddr_in sockaddr) : socketaddr{sockaddr} {}

  std::vector<std::byte> handle_list() {
    namespace fs = std::filesystem;
    std::vector<std::byte> to_return;

    ListRes res;

    for (const auto& entry : fs::directory_iterator(".")) {
      auto desc = FileDesc{entry.file_size()};
      auto ss = entry.path().filename().string();
      desc.filename_length = ss.size();
      strncpy(desc.filename, ss.c_str(), FileDesc::namesize);
      res.files.push_back(desc);
    }

    auto ss = res.serialize_to(nullptr);
    to_return.resize(ss);

    res.serialize_to(to_return.data());

    return to_return;
  }

  std::vector<std::byte> handle_one_command(Command cmd) {
    switch (cmd.command_enum) {
      case CommandEnum::list:
        return handle_list();
    }
    std::cout << "Not recognize command !\n";
    return {};
  }

  void receive(const std::byte* buf, size_t n) { msg_parser.receive(buf, n); }

  void handle_one(auto&& func) {  // writer func

    while (auto d = msg_parser.pop()) {
      auto msg = handle_one_command(d.value());
      func(msg.data(), msg.size());
    }
  }

 private:
  TcpMsgParser msg_parser;
  sockaddr_in socketaddr;
  std::deque<Command> commands;
};

class TcpServer {
  static constexpr int MAX_SIZE = 100;
  static constexpr char hello[] = "HelloWorld";

 public:
  TcpServer() {
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1) {
      error("socket");
    }

    int opt = 1;
    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
      error("setsockopt");
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(12345);
    int res =
        bind(tcp_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (res < 0) {
      error("bind");
    }

    if (listen(tcp_socket, 10) < 0) {
      error("listen");
    }

    epoll_fd = epoll_create(MAX_SIZE);
    if (epoll_fd == -1) {
      error("epoll_create");
    }

    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = tcp_socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_socket, &ev) == -1) {
      error("epoll_ctl");
    }
  }

  ~TcpServer() {
    for (auto& [fd, addr] : fd_to_address) {
      close(fd);
    }
    close(tcp_socket);
    close(epoll_fd);
  }

  void shutdown() { stop.store(true); }

  void loop() {
    while (!stop.load()) {
      int nfds = epoll_wait(epoll_fd, ev_events, MAX_SIZE, 1000);
      if (nfds == -1) {
        if (errno == EINTR)
          continue;
        error("epoll_wait");
      }
      for (int i = 0; i < nfds; i++) {
        if (ev_events[i].data.fd == tcp_socket) {
          handle_accept();
        } else {
          handle_connection(ev_events[i]);
        }
      }
    }
    std::cout << "finished" << std::endl;
  }

  void handle_accept() {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t len = sizeof(sockaddr_in);
    int socket = accept(tcp_socket, reinterpret_cast<sockaddr*>(&addr), &len);

    if (socket < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return;
      }
      error("accept");
    }

    fd_to_address.emplace(socket, addr);

    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket, &ev) == -1) {
      close(socket);
      fd_to_address.erase(socket);
      error("epoll_ctl");
    }
  }

  void cleanup_connection_fd(int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    fd_to_address.erase(fd);
  }

  void handle_connection(epoll_event event) {
    std::byte buffer[30000];
    ssize_t valread = ::read(event.data.fd, buffer, sizeof(buffer) - 1);
    if (valread <= 0) {
      if (valread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      cleanup_connection_fd(event.data.fd);
      return;
    }
    std::cout << "HERE " << valread;
    Context& context = fd_to_address.at(event.data.fd);
    context.receive(buffer, valread);
    context.handle_one([this, &event](const std::byte* buf, size_t n) {
      write_all(event.data.fd, buf, n);
    });
  }

 private:
  bool write_all(int fd, const std::byte* buf, size_t len) {
    size_t total_written = 0;
    while (total_written < len) {
      ssize_t written = ::write(fd, buf + total_written, len - total_written);
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        cleanup_connection_fd(fd);
        return false;
      }
      total_written += written;
    }
    return true;
  }

  int tcp_socket;
  int epoll_fd;
  std::atomic<bool> stop{false};
  epoll_event ev_events[MAX_SIZE];
  std::unordered_map<int, Context> fd_to_address;
};

}  // namespace MyFtp

static MyFtp::TcpServer* g_server = nullptr;

void signal_handler(int sig) {
  if (g_server) {
    g_server->shutdown();
  }
}

int main() {
  MyFtp::TcpServer server{};
  g_server = &server;

  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  server.loop();
  g_server = nullptr;
}