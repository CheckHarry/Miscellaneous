#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#define error(msg)                                                            \
  {                                                                           \
    std::cerr << "error at line : " << __LINE__ << " : " << msg << std::endl; \
    std::exit(-1);                                                            \
  }

namespace MyFtp {
class FileProtocolController {
 public:
 private:
};

struct AcceptParams {
  sockaddr addr;
};

class TcpServer {
  static constexpr int MAX_SIZE = 100;
  static constexpr char hello[] = "HelloWorld";

 public:
  TcpServer() {
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1) {
      std::exit(-1);
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(sockaddr));
    socklen_t len{};
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

    ev.events = EPOLLIN;
    ev.data.fd = tcp_socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_socket, &ev) == -1) {
      error("epoll_ctl");
    }
  }

  void loop() {
    while (!stop.load()) {
      int nfds = epoll_wait(epoll_fd, ev_events, MAX_SIZE, -1);
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
    unsigned int len = sizeof(sockaddr_in);
    int socket = accept(tcp_socket, reinterpret_cast<sockaddr*>(&addr), &len);

    if (socket < 0) {
      error("accept");
    }
    fd_to_address[socket] = addr;

    ev.events = EPOLLIN;
    ev.data.fd = socket;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket, &ev) == -1) {
      error("epoll_ctl");
    }
  }

  void cleanup_connection_fd(int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    fd_to_address.erase(fd);
  }

  void handle_connection(epoll_event event) {
    char buffer[30000] = {0};
    int valread = ::read(event.data.fd, buffer, 30000);
    if (valread > 0) {
      printf("%s\n", buffer);
    } else if (valread <= 0) {
      if (valread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;
      }
      cleanup_connection_fd(event.data.fd);
      return;
    }

    write(event.data.fd, hello, strlen(hello));
  }

 private:
  int tcp_socket;
  int epoll_fd;
  std::atomic<bool> stop{false};
  epoll_event ev, ev_events[MAX_SIZE];
  std::unordered_map<int, sockaddr_in> fd_to_address;
};

}  // namespace MyFtp

int main() {

  MyFtp::TcpServer accept_loop{};
  accept_loop.loop();
}