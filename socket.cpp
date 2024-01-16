#include "socket.h"
#include "iomanager.h"
#include "fdmanager.h"
#include "log.h"
#include "macro.h"
#include "hook.h"
#include <limits.h>
#include <sstream>
#include <netdb.h>
#include <ifaddrs.h>
#include <stddef.h>

namespace fisher {

static Logger::LoggerRef g_logger = FISHER_LOG_NAME("system");
static const size_t MAX_PATH_LEN = sizeof(((sockaddr_un*)0)->sun_path) - 1;

template<class T>
static T CreateMask(uint32_t bits) {
    return (1 << (sizeof(T) * 8 - bits)) - 1;
}

template<class T>
static uint32_t CountBytes(T value) {
    uint32_t result = 0;
    for(; value; ++result) {
        value &= value - 1;
    }
    return result;
}

int Address::getFamily() const {
    return getAddr()->sa_family;
}

std::string Address::toString() const {
    std::stringstream ss;
    insert(ss);
    return ss.str();
}

Address::AddressRef Address::Create(const sockaddr* addr, socklen_t addrlen) {
    if(addr == nullptr) {
        return nullptr;
    }
    return std::make_shared<Address>(*(const sockaddr_in*)addr);
}


bool Address::operator==(const Address& rhs) const {
    return getAddrLen() == rhs.getAddrLen()
        && memcmp(getAddr(), rhs.getAddr(), getAddrLen()) == 0;
}

Address::AddressRef Address::Create(const char* address, uint16_t port) {
    Address::AddressRef rt = std::make_shared<Address>(AF_INET);
    rt->m_addr.sin_port = byteswapOnLittleEndian(port);
    int result = inet_pton(AF_INET, address, &rt->m_addr.sin_addr);
    if(result <= 0) {
        FISHER_LOG_DEBUG(g_logger) << "IPv4Address::Create(" << address << ", "
                << port << ") rt=" << result << " errno=" << errno
                << " errstr=" << strerror(errno);
        return nullptr;
    }
    return rt;
}

Address::Address(const sockaddr_in& address) {
    m_addr = address;
}

Address::Address(uint32_t address, uint16_t port) {
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.sin_family = AF_INET;
    m_addr.sin_port = byteswapOnLittleEndian(port);
    m_addr.sin_addr.s_addr = byteswapOnLittleEndian(address);
}

const sockaddr* Address::getAddr() const {
    return (sockaddr*)&m_addr;
}

sockaddr* Address::getAddr() {
    return (sockaddr*)&m_addr;
}

socklen_t Address::getAddrLen() const {
    return sizeof(m_addr);
}

std::ostream& Address::insert(std::ostream& os) const {
    uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
    os << ((addr >> 24) & 0xff) << "."
       << ((addr >> 16) & 0xff) << "."
       << ((addr >> 8) & 0xff) << "."
       << (addr & 0xff);
    os << ":" << byteswapOnLittleEndian(m_addr.sin_port);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Address& addr) {
    return addr.insert(os);
}


Socket::SocketRef Socket::CreateTCP(Address::AddressRef address) {
    Socket::SocketRef sock = std::make_shared<Socket>(address->getFamily(), TCP, 0);
    return sock;
}

Socket::SocketRef Socket::CreateUDP(Address::AddressRef address) {
    Socket::SocketRef sock = std::make_shared<Socket>(address->getFamily(), UDP, 0);
    sock->newSock();
    sock->isConnected_ = true;
    return sock;
}

Socket::SocketRef Socket::CreateTCPSocket() {
    Socket::SocketRef sock = std::make_shared<Socket>(IPv4, TCP, 0);
    return sock;
}

Socket::SocketRef Socket::CreateUDPSocket() {
    Socket::SocketRef sock = std::make_shared<Socket>(IPv4, UDP, 0);
    sock->newSock();
    sock->isConnected_ = true;
    return sock;
}

Socket::Socket(int family, int type, int protocol)
    :sock_(-1)
    ,family_(family)
    ,type_(type)
    ,protocol_(protocol)
    ,isConnected_(false) {
}

Socket::~Socket() {
    close();
}

int64_t Socket::getSendTimeout() {
    FdCtx::FdCtxRef ctx = FdMgr::getInstance().get(sock_);
    if(ctx) {
        return ctx->getTimeout(SO_SNDTIMEO);
    }
    return -1;
}

void Socket::setSendTimeout(int64_t v) {
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_SNDTIMEO, tv);
}

int64_t Socket::getRecvTimeout() {
    FdCtx::FdCtxRef ctx = FdMgr::getInstance().get(sock_);
    if(ctx) {
        return ctx->getTimeout(SO_RCVTIMEO);
    }
    return -1;
}

void Socket::setRecvTimeout(int64_t v) {
    struct timeval tv{int(v / 1000), int(v % 1000 * 1000)};
    setOption(SOL_SOCKET, SO_RCVTIMEO, tv);
}

bool Socket::getOption(int level, int option, void* result, socklen_t* len) {
    int rt = getsockopt(sock_, level, option, result, (socklen_t*)len);
    if(rt) {
        FISHER_LOG_DEBUG(g_logger) << "getOption sock=" << sock_
            << " level=" << level << " option=" << option
            << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::setOption(int level, int option, const void* result, socklen_t len) {
    if(setsockopt(sock_, level, option, result, (socklen_t)len)) {
        FISHER_LOG_DEBUG(g_logger) << "setOption sock=" << sock_
            << " level=" << level << " option=" << option
            << " errno=" << errno << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

Socket::SocketRef Socket::accept() {
    Socket::SocketRef sock = std::make_shared<Socket>(family_, type_, protocol_);
    int newsock = ::accept(sock_, nullptr, nullptr);
    if(newsock == -1) {
        FISHER_LOG_ERROR(g_logger) << "accept(" << sock_ << ") errno="
            << errno << " errstr=" << strerror(errno);
        return nullptr;
    }
    if(sock->init(newsock)) {
        return sock;
    }
    return nullptr;
}

bool Socket::init(int sock) {
    FdCtx::FdCtxRef ctx = FdMgr::getInstance().get(sock);
    if(ctx && ctx->isSocket() && !ctx->isClose()) {
        sock_ = sock;
        isConnected_ = true;
        initSock();
        getLocalAddress();
        getRemoteAddress();
        return true;
    }
    return false;
}

bool Socket::bind(const Address::AddressRef addr) {
    //m_localAddress = addr;
    if(!isValid()) {
        newSock();
        if(SYLAR_UNLIKELY(!isValid())) {
            return false;
        }
    }

    if(__glibc_unlikely(addr->getFamily() != family_)) {
        FISHER_LOG_ERROR(g_logger) << "bind sock.family("
            << family_ << ") addr.family(" << addr->getFamily()
            << ") not equal, addr=" << addr->toString();
        return false;
    }

    if(::bind(sock_, addr->getAddr(), addr->getAddrLen())) {
        FISHER_LOG_ERROR(g_logger) << "bind error errrno=" << errno
            << " errstr=" << strerror(errno);
        return false;
    }
    getLocalAddress();
    return true;
}

bool Socket::reconnect(uint64_t timeout_ms) {
    if(!remoteAddress_) {
        FISHER_LOG_ERROR(g_logger) << "reconnect m_remoteAddress is null";
        return false;
    }
    localAddress_.reset();
    return connect(remoteAddress_, timeout_ms);
}

bool Socket::connect(const Address::AddressRef addr, uint64_t timeout_ms) {
    remoteAddress_ = addr;
    if(!isValid()) {
        newSock();
        if(__glibc_unlikely(!isValid())) {
            return false;
        }
    }

    if(__glibc_unlikely(addr->getFamily() != family_)) {
        FISHER_LOG_ERROR(g_logger) << "connect sock.family("
            << family_ << ") addr.family(" << addr->getFamily()
            << ") not equal, addr=" << addr->toString();
        return false;
    }

    if(timeout_ms == (uint64_t)-1) {
        if(::connect(sock_, addr->getAddr(), addr->getAddrLen())) {
            FISHER_LOG_ERROR(g_logger) << "sock=" << sock_ << " connect(" << addr->toString()
                << ") error errno=" << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    } else {
        if(::connect_with_timeout(sock_, addr->getAddr(), addr->getAddrLen(), timeout_ms)) {
            FISHER_LOG_ERROR(g_logger) << "sock=" << sock_ << " connect(" << addr->toString()
                << ") timeout=" << timeout_ms << " error errno="
                << errno << " errstr=" << strerror(errno);
            close();
            return false;
        }
    }
    isConnected_ = true;
    getRemoteAddress();
    getLocalAddress();
    return true;
}

bool Socket::listen(int backlog) {
    if(!isValid()) {
        FISHER_LOG_ERROR(g_logger) << "listen error sock=-1";
        return false;
    }
    if(::listen(sock_, backlog)) {
        FISHER_LOG_ERROR(g_logger) << "listen error errno=" << errno
            << " errstr=" << strerror(errno);
        return false;
    }
    return true;
}

bool Socket::close() {
    if(!isConnected_ && sock_ == -1) {
        return true;
    }
    isConnected_ = false;
    if(sock_ != -1) {
        ::close(sock_);
        sock_ = -1;
    }
    return false;
}

int Socket::send(const void* buffer, size_t length, int flags) {
    if(isConnected()) {
        return ::send(sock_, buffer, length, flags);
    }
    return -1;
}

int Socket::send(const iovec* buffers, size_t length, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::sendmsg(sock_, &msg, flags);
    }
    return -1;
}

int Socket::sendTo(const void* buffer, size_t length, const Address::AddressRef to, int flags) {
    if(isConnected()) {
        return ::sendto(sock_, buffer, length, flags, to->getAddr(), to->getAddrLen());
    }
    return -1;
}

int Socket::sendTo(const iovec* buffers, size_t length, const Address::AddressRef to, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = to->getAddr();
        msg.msg_namelen = to->getAddrLen();
        return ::sendmsg(sock_, &msg, flags);
    }
    return -1;
}

int Socket::recv(void* buffer, size_t length, int flags) {
    if(isConnected()) {
        return ::recv(sock_, buffer, length, flags);
    }
    return -1;
}

int Socket::recv(iovec* buffers, size_t length, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(sock_, &msg, flags);
    }
    return -1;
}

int Socket::recvFrom(void* buffer, size_t length, Address::AddressRef from, int flags) {
    if(isConnected()) {
        socklen_t len = from->getAddrLen();
        return ::recvfrom(sock_, buffer, length, flags, from->getAddr(), &len);
    }
    return -1;
}

int Socket::recvFrom(iovec* buffers, size_t length, Address::AddressRef from, int flags) {
    if(isConnected()) {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec*)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = from->getAddr();
        msg.msg_namelen = from->getAddrLen();
        return ::recvmsg(sock_, &msg, flags);
    }
    return -1;
}

Address::AddressRef Socket::getRemoteAddress() {
    if(remoteAddress_) {
        return remoteAddress_;
    }

    Address::AddressRef result = std::make_shared<Address>(family_);
    socklen_t addrlen = result->getAddrLen();
    getpeername(sock_, result->getAddr(), &addrlen);
    remoteAddress_ = result;
    return remoteAddress_;
}

Address::AddressRef Socket::getLocalAddress() {
    if(localAddress_) {
        return localAddress_;
    }

    Address::AddressRef result;
    result.reset(new Address(family_));
    socklen_t addrlen = result->getAddrLen();
    if(getsockname(sock_, result->getAddr(), &addrlen)) {
        FISHER_LOG_ERROR(g_logger) << "getsockname error sock=" << sock_
            << " errno=" << errno << " errstr=" << strerror(errno);
        return std::make_shared<Address>(family_);
    }

    localAddress_ = result;
    return localAddress_;
}

bool Socket::isValid() const {
    return sock_ != -1;
}

int Socket::getError() {
    int error = 0;
    socklen_t len = sizeof(error);
    if(!getOption(SOL_SOCKET, SO_ERROR, &error, &len)) {
        error = errno;
    }
    return error;
}

std::ostream& Socket::dump(std::ostream& os) const {
    os << "[Socket sock=" << sock_
       << " is_connected=" << isConnected_
       << " family=" << family_
       << " type=" << type_
       << " protocol=" << protocol_;
    if(localAddress_) {
        os << " local_address=" << localAddress_->toString();
    }
    if(remoteAddress_) {
        os << " remote_address=" << remoteAddress_->toString();
    }
    os << "]";
    return os;
}

std::string Socket::toString() const {
    std::stringstream ss;
    dump(ss);
    return ss.str();
}

bool Socket::cancelRead() {
    return IOManager::GetThis()->cancelEvent(sock_, IOManager::READ);
}

bool Socket::cancelWrite() {
    return IOManager::GetThis()->cancelEvent(sock_, IOManager::WRITE);
}

bool Socket::cancelAccept() {
    return IOManager::GetThis()->cancelEvent(sock_, IOManager::READ);
}

bool Socket::cancelAll() {
    return IOManager::GetThis()->cancelAll(sock_);
}

void Socket::initSock() {
    int val = 1;
    setOption(SOL_SOCKET, SO_REUSEADDR, val);
    if(type_ == SOCK_STREAM) {
        setOption(IPPROTO_TCP, TCP_NODELAY, val);
    }
}

void Socket::newSock() {
    sock_ = socket(family_, type_, protocol_);
    if(__glibc_likely(sock_ != -1)) {
        initSock();
    } else {
        FISHER_LOG_ERROR(g_logger) << "socket(" << family_
            << ", " << type_ << ", " << protocol_ << ") errno="
            << errno << " errstr=" << strerror(errno);
    }
}


std::ostream& operator<<(std::ostream& os, const Socket& sock) {
    return sock.dump(os);
}

}