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
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define error(msg)                                                                \
    {                                                                             \
        std::cerr << "error at line : " << __LINE__ << " : " << msg << std::endl; \
        std::exit(-1);                                                            \
    }

#define debug(msg)                              \
    {                                           \
        std::cerr << "[" << __LINE__ << "]"     \
                  << " : " << msg << std::endl; \
    }

namespace MyFtp {

struct FileDesc {
    static constexpr std::uint64_t namesize = 256;
    std::uint64_t size;
    std::uint8_t filename_length;
    char filename[namesize];

    size_t serialize_to(
        std::byte* ptr) const {  // ptr must have enough size , passing nullptr for realsize
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

struct DownloadRes {
    bool success = false;
    std::vector<std::byte> filedata;

    size_t serialize_to(std::byte* ptr) const {
        if (!success) {
            if (ptr) {
                memset(ptr, 0, 1);
            }
            return sizeof(bool);
        } else {
            if (ptr) {
                memset(ptr, 1, 1);
                ptr += 1;
                size_t ss = filedata.size();
                memcpy(ptr, &ss, sizeof(ss));
                ptr += sizeof(ss);
                memcpy(ptr, filedata.data(), filedata.size());
            }
            return sizeof(bool) + sizeof(size_t) + filedata.size();
        }
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

enum class CommandEnum : uint8_t { list = 1, download = 2 };

// B stand for binary protocol , we rely on this class to parse data from tcp
struct __attribute__((packed)) CommandBaseB {
    CommandEnum command_enum;
};

struct __attribute__((packed)) DownloadCommandB {
    CommandBaseB commandbase;
    std::uint8_t filename_size = 0;
};

#define DEFINE_VIRTUAL_GETTER(name) \
    virtual name* get##name() {     \
        return nullptr;             \
    }
struct ListCommand;
struct DownloadCommand;

struct Command {
public:
    virtual ~Command() = default;
    // virtual DownloadCommand* getDownloadCommand() {return nullptr;}
    DEFINE_VIRTUAL_GETTER(DownloadCommand)
    DEFINE_VIRTUAL_GETTER(ListCommand)
private:
};

struct DownloadCommand : public Command {
    DownloadCommand(std::string f) : filename(f) {}
    std::string filename;
    DownloadCommand* getDownloadCommand() override {
        return this;
    }
};

struct ListCommand : public Command {
    ListCommand* getListCommand() override {
        return this;
    }
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

    int must_read(std::byte* rec,
                  size_t n) {  // must read exactly n bytes , return 0 upon success , else -1
        if ((tail - head) < n) return -1;
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

class CommandParser {
public:
    CommandParser(CircularBuffer& cb) : cbuf{cb} {}

    void reset() {
        state = State::Init;
        filename_size = 0;
    }

    bool handle_init() {
        std::byte buf[4096];
        if (cbuf.must_read(buf, sizeof(CommandEnum)) != 0) return false;
        auto cmdenum = *reinterpret_cast<CommandEnum*>(&buf);
        switch (cmdenum) {
            case CommandEnum::list:
                cmdeq.push_back(std::make_unique<ListCommand>());
                reset();
                break;
            case CommandEnum::download:
                state = State::ParseDownloadFileNameSize;
                break;
            default:
                error("invalid command");
        }
        return true;
    }

    bool handle_parse_download_file_name_size() {
        std::byte buf[4096];
        if (cbuf.must_read(buf, sizeof(std::uint8_t)) != 0) return false;
        filename_size = *reinterpret_cast<std::uint8_t*>(&buf) + 1;
        debug((int)filename_size) state = State::ParseDownloadFileName;
        return true;
    }

    bool handle_parse_download_file_name() {
        std::byte buf[4096]{};
        if (cbuf.must_read(buf, filename_size) != 0) return false;
        std::string filename(reinterpret_cast<char*>(&buf), filename_size);
        debug(filename);
        cmdeq.push_back(std::make_unique<DownloadCommand>(filename));
        reset();
        return true;
    }

    bool exhaust_one() {
        switch (state) {
            case State::Init:
                return handle_init();
            case State::ParseDownloadFileNameSize:
                return handle_parse_download_file_name_size();
            case State::ParseDownloadFileName:
                return handle_parse_download_file_name();
        }
        error("invalid state");
    }

    void exhasut() {
        while (exhaust_one()) {
        }
    }

    std::unique_ptr<Command> pop() {
        if (cmdeq.size()) {
            auto res = std::move(cmdeq.front());
            cmdeq.pop_front();
            return res;
        }
        return nullptr;
    }

private:
    std::deque<std::unique_ptr<Command>> cmdeq;
    CircularBuffer& cbuf;
    // if commandEnum == List , not need to parse further
    // if commandEnum == Download , parse
    enum class State {
        Init,  // no
        ParseDownloadFileNameSize,
        ParseDownloadFileName
    };

    State state;
    std::uint8_t filename_size = 0;  // only meaning ful when state = ParseDownloadFileName;
};

class TcpMsgParser {
public:
    void exahust() {
        parser.exhasut();
    }

    void receive(const std::byte* buf, size_t len) {
        exahust();
        assert(cbuf.push(buf, len) == len);
    }

    std::unique_ptr<Command> pop() {
        exahust();
        return parser.pop();
    }

private:
    CircularBuffer cbuf;
    CommandParser parser{cbuf};
};

template <typename T>
std::vector<std::byte> serialize_to(const T& cmd) {
    std::vector<std::byte> to_return;
    size_t ss = cmd.serialize_to(nullptr);
    to_return.resize(ss);
    cmd.serialize_to(to_return.data());
    return to_return;
}

struct Context {
    Context(sockaddr_in sockaddr) : socketaddr{sockaddr} {}

    std::vector<std::byte> handle_list() {
        namespace fs = std::filesystem;
        ListRes res;

        for (const auto& entry : fs::directory_iterator(".")) {
            auto desc = FileDesc{entry.file_size()};
            auto ss = entry.path().filename().string();
            desc.filename_length = ss.size();
            strncpy(desc.filename, ss.c_str(), FileDesc::namesize);
            res.files.push_back(desc);
        }

        return serialize_to(res);
    }

    std::vector<std::byte> handle_download(DownloadCommand cmd) {
        namespace fs = std::filesystem;

        DownloadRes res;
        std::ifstream file(cmd.filename, std::ios::binary | std::ios::ate);
        debug(cmd.filename);
        if (!file.is_open()) {
            debug("Cannot open file") return serialize_to(res);
        }

        std::streamsize fileSize = file.tellg();
        debug(fileSize);
        file.seekg(0, std::ios::beg);
        res.filedata.resize(fileSize);
        if (file.read(reinterpret_cast<char*>(res.filedata.data()), fileSize)) {
            res.success = true;
        }

        file.close();
        return serialize_to(res);
    }

    std::vector<std::byte> handle_one_command(Command* cmd) {
        if (cmd->getListCommand()) {
            return handle_list();
        }

        if (cmd->getDownloadCommand()) {
            return handle_download(*cmd->getDownloadCommand());
        }
        std::cout << "Not recognize command !\n";
        return {};
    }

    void receive(const std::byte* buf, size_t n) {
        msg_parser.receive(buf, n);
    }

    void handle_one(auto&& func) {  // writer func
        while (auto d = msg_parser.pop()) {
            auto msg = handle_one_command(d.get());
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
        if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            error("setsockopt");
        }

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(12345);
        int res = bind(tcp_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
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

    void shutdown() {
        stop.store(true);
    }

    void loop() {
        while (!stop.load()) {
            int nfds = epoll_wait(epoll_fd, ev_events, MAX_SIZE, 1000);
            if (nfds == -1) {
                if (errno == EINTR) continue;
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

        Context& context = fd_to_address.at(event.data.fd);
        context.receive(buffer, valread);
        context.handle_one(
            [this, &event](const std::byte* buf, size_t n) { write_all(event.data.fd, buf, n); });
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