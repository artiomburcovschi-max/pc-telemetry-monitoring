
// Compile Linux:   g++ -O3 -std=c++17 -pthread -o netscan netscan.cpp
// Compile Windows: g++ -O3 -std=c++17 -o netscan.exe netscan.cpp -liphlpapi -lws2_32

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <icmpapi.h>
#  pragma comment(lib, "iphlpapi.lib")
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <netinet/in.h>
#  include <netinet/ip_icmp.h>
#  include <arpa/inet.h>
#  include <net/if.h>
#  include <ifaddrs.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <poll.h>
#  ifdef __linux__
#    include <netpacket/packet.h>
#    include <linux/if_ether.h>
#    include <net/ethernet.h>
#  endif
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <functional>
#include <queue>
#include <condition_variable>

using namespace std;
using namespace std::chrono;

class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                for(;;) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty()) return;
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }
    template<class F> void enqueue(F&& f) {
        {
            unique_lock<mutex> lock(queue_mutex);
            tasks.emplace(forward<F>(f));
        }
        condition.notify_one();
    }
    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(thread &worker: workers) worker.join();
    }
private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;
};
static const map<string, const char*> OUI_TABLE = {
    {"00:03:93","Apple"}, {"00:0A:27","Apple"}, {"00:0A:95","Apple"},
    {"00:11:24","Apple"}, {"00:26:BB","Apple"}, {"00:30:65","Apple"},
    {"00:00:F0","Samsung"}, {"00:02:78","Samsung"}, {"8C:71:F8","Samsung"},
    {"00:9E:C8","Xiaomi"}, {"04:CF:8C","Xiaomi"}, {"D4:97:0B","Xiaomi"},
    {"00:1D:0F","TP-Link"}, {"14:CC:20","TP-Link"}, {"F0:9F:C2","TP-Link"},
    {"00:18:82","Huawei"}, {"00:1E:10","Huawei"}, {"F4:4C:7F","Huawei"},
    {"00:0C:6E","ASUS"}, {"00:0E:A6","ASUS"}, {"BC:AE:C5","ASUS"},
    {"00:05:5D","D-Link"}, {"00:0D:88","D-Link"}, {"F8:1A:67","D-Link"},
    {"28:28:5D","Keenetic"}, {"30:B4:9E","Keenetic"}, {"50:FF:20","Keenetic"},
    {"00:0C:42","MikroTik"}, {"18:FD:74","MikroTik"}, {"E4:8D:8C","MikroTik"},
    {"00:1A:11","Google"}, {"08:9E:08","Google"}, {"F4:F5:D8","Google"},
    {"00:BB:3A","Amazon"}, {"0C:47:C9","Amazon"}, {"F0:27:2D","Amazon"},
    {"1C:C3:16","Hikvision"}, {"28:57:BE","Hikvision"},
    {"00:12:34","Dahua"}, {"3C:EF:8C","Dahua"},
    {"00:E0:4C","Realtek"},
    {"00:02:B3","Intel"}, {"00:03:47","Intel"}, {"AC:FD:CE","Intel"},
    {"00:0C:29","VMware"}, {"00:50:56","VMware"}
};
string get_vendor(const string& mac) {
    if (mac.size() < 8) return "Unknown";
    string oui = mac.substr(0, 8);
    transform(oui.begin(), oui.end(), oui.begin(), ::toupper);
    auto it = OUI_TABLE.find(oui);
    if (it != OUI_TABLE.end()) return it->second;
    return "Unknown";
}
string ip_to_str(uint32_t ip) {
    char buf[INET_ADDRSTRLEN];
    struct in_addr addr;
    addr.s_addr = ip;
    inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN);
    return string(buf);
}
string mac_to_str(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return buf;
}
struct HostResult {
    string ip;
    string mac;
    string vendor;
    long   ping_ms;
};

mutex results_mutex;
vector<HostResult> results;
//  LINUX
#ifndef _WIN32

#pragma pack(push, 1)
struct ArpEthFrame {
    uint8_t  eth_dst[6];
    uint8_t  eth_src[6];
    uint16_t eth_type;
    uint16_t hw_type;
    uint16_t proto;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint8_t  sender_ip[4];
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
};
#pragma pack(pop)

static const uint8_t BCAST6[6]  = {0xff,0xff,0xff,0xff,0xff,0xff};
static const uint8_t ZERO6[6]   = {0,0,0,0,0,0};

struct NetInfo {
    string   iface;
    uint32_t local_ip;
    uint32_t netmask;
    uint8_t  local_mac[6];
    int      ifindex;
    bool     has_raw;
};

bool get_net_info(NetInfo& ni) {
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) < 0) return false;

    bool found_ip = false;
    string target;

    for (auto* a = ifa; a; a = a->ifa_next) {
        if (!a->ifa_addr || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_UP) || a->ifa_addr->sa_family != AF_INET) continue;
        auto* sa  = reinterpret_cast<sockaddr_in*>(a->ifa_addr);
        auto* snm = reinterpret_cast<sockaddr_in*>(a->ifa_netmask);
        ni.local_ip = sa->sin_addr.s_addr;
        ni.netmask  = snm ? snm->sin_addr.s_addr : htonl(0xFFFFFF00);
        target      = a->ifa_name;
        found_ip    = true;
        break;
    }
    if (!found_ip) { freeifaddrs(ifa); return false; }

    bool found_mac = false;
    for (auto* a = ifa; a; a = a->ifa_next) {
        if (!a->ifa_addr || string(a->ifa_name) != target) continue;
        if (a->ifa_addr->sa_family == AF_PACKET) {
            auto* sll = reinterpret_cast<sockaddr_ll*>(a->ifa_addr);
            if (sll->sll_halen == 6) {
                memcpy(ni.local_mac, sll->sll_addr, 6);
                found_mac = true;
            }
        }
    }
    freeifaddrs(ifa);

    ni.iface   = target;
    ni.ifindex = if_nametoindex(target.c_str());

    int ts = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    ni.has_raw = (ts >= 0);
    if (ts >= 0) close(ts);

    return found_ip && found_mac;
}

map<uint32_t, array<uint8_t,6>> arp_scan_raw(const NetInfo& ni, int timeout_ms) {
    map<uint32_t, array<uint8_t,6>> found;
    int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sock < 0) return found;

    struct sockaddr_ll bind_addr{};
    bind_addr.sll_family   = AF_PACKET;
    bind_addr.sll_protocol = htons(ETH_P_ARP);
    bind_addr.sll_ifindex  = ni.ifindex;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        close(sock); return found;
    }

    uint32_t netw    = ni.local_ip & ni.netmask;
    uint32_t hostmask = ~ni.netmask;
    uint32_t count   = ntohl(hostmask) - 1;
    if (count > 1022) count = 253; 

    struct sockaddr_ll dest{};
    dest.sll_family   = AF_PACKET;
    dest.sll_protocol = htons(ETH_P_ARP);
    dest.sll_ifindex  = ni.ifindex;
    dest.sll_halen    = 6;
    memcpy(dest.sll_addr, BCAST6, 6);

    for (uint32_t h = 1; h <= count; h++) {
        uint32_t tip = htonl(ntohl(netw) + h);
        ArpEthFrame f{};
        memcpy(f.eth_dst, BCAST6, 6);
        memcpy(f.eth_src, ni.local_mac, 6);
        f.eth_type  = htons(ETH_P_ARP);
        f.hw_type   = htons(1);
        f.proto     = htons(0x0800);
        f.hw_len    = 6; f.proto_len = 4;
        f.opcode    = htons(1);
        memcpy(f.sender_mac, ni.local_mac, 6);
        memcpy(f.sender_ip,  &ni.local_ip, 4);
        memcpy(f.target_mac, ZERO6, 6);
        memcpy(f.target_ip,  &tip, 4);
        sendto(sock, &f, sizeof(f), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        usleep(300);
    }

    auto deadline = steady_clock::now() + milliseconds(timeout_ms);
    while (steady_clock::now() < deadline) {
        auto rem = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
        if (rem <= 0) break;
        struct pollfd pfd{sock, POLLIN, 0};
        if (poll(&pfd, 1, (int)rem) <= 0) continue;

        ArpEthFrame buf{};
        ssize_t n = recv(sock, &buf, sizeof(buf), 0);
        if (n < (ssize_t)sizeof(buf) || ntohs(buf.opcode) != 2) continue;

        uint32_t sip;
        memcpy(&sip, buf.sender_ip, 4);
        array<uint8_t,6> smac;
        memcpy(smac.data(), buf.sender_mac, 6);
        found[sip] = smac;
    }
    close(sock);
    return found;
}

struct IcmpHeader { uint8_t type, code; uint16_t checksum, id, seq; };

uint16_t icmp_checksum(const void* data, size_t len) {
    uint32_t sum = 0;
    const uint16_t* p = reinterpret_cast<const uint16_t*>(data);
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *reinterpret_cast<const uint8_t*>(p);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

long ping_linux(uint32_t ip, int timeout_ms) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    bool dgram_mode = (sock >= 0);
    if (sock < 0) {
        sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (sock < 0) return -1;
    }

    struct timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    uint16_t pid = (uint16_t)(getpid() & 0xFFFF);
    IcmpHeader req{};
    req.type = 8; req.code = 0; req.id = htons(pid); req.seq = htons(1);
    req.checksum = icmp_checksum(&req, sizeof(req));

    struct sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = ip;

    auto t0 = steady_clock::now();
    if (sendto(sock, &req, sizeof(req), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) < 0) {
        close(sock); return -1;
    }

    uint8_t rbuf[64] = {};
    struct sockaddr_in src{};
    socklen_t slen = sizeof(src);
    ssize_t n = recvfrom(sock, rbuf, sizeof(rbuf), 0, reinterpret_cast<sockaddr*>(&src), &slen);
    close(sock);
    if (n < 0) return -1;

    auto ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    IcmpHeader* rep = dgram_mode ? reinterpret_cast<IcmpHeader*>(rbuf) : reinterpret_cast<IcmpHeader*>(rbuf + 20);
    if (rep->type != 0) return -1; 
    return ms;
}

long tcp_probe(uint32_t ip, int timeout_ms) {
    static const int PORTS[] = {80, 443, 22, 445, 8080, 23, 21, 3389, 0};
    for (int i = 0; PORTS[i]; i++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) continue;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(PORTS[i]);
        addr.sin_addr.s_addr = ip;

        auto t0 = steady_clock::now();
        connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        struct pollfd pfd{s, POLLOUT | POLLERR, 0};
        int r = poll(&pfd, 1, timeout_ms / (int)(sizeof(PORTS)/sizeof(int)-1));

        long ms = -1;
        if (r > 0) {
            int err = 0; socklen_t el = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err == 0 || err == ECONNREFUSED) {
                ms = duration_cast<milliseconds>(steady_clock::now() - t0).count();
            }
        }
        close(s);
        if (ms >= 0) return ms;
    }
    return -1;
}

string mac_from_arp_cache(const string& ip_str) {
    ifstream f("/proc/net/arp");
    if (!f) return "";
    string line;
    getline(f, line); 
    while (getline(f, line)) {
        istringstream ss(line);
        string ip, hw, flags, mac;
        ss >> ip >> hw >> flags >> mac;
        if (ip == ip_str && mac != "00:00:00:00:00:00") {
            transform(mac.begin(), mac.end(), mac.begin(), ::toupper);
            return mac;
        }
    }
    return "";
}

void scan_host_linux(uint32_t ip_n, int timeout_ms) {
    string ip_str = ip_to_str(ip_n);
    long ms = ping_linux(ip_n, min(timeout_ms / 4, 500));
    if (ms < 0) {
        ms = tcp_probe(ip_n, timeout_ms / 2);
        if (ms < 0) return; 
    }
    string mac = mac_from_arp_cache(ip_str);
    if (mac.empty()) mac = "??:??:??:??:??:??";
    string vendor = get_vendor(mac);

    lock_guard<mutex> lg(results_mutex);
    results.push_back({ip_str, mac, vendor, ms});
}

void run_linux(int timeout_ms) {
    NetInfo ni;
    if (!get_net_info(ni)) { cerr << "ERR: Не найден сетевой интерфейс\n"; return; }

    uint32_t netw     = ni.local_ip & ni.netmask;
    uint32_t hostmask = ~ni.netmask;
    uint32_t count    = ntohl(hostmask) - 1;
    if (count > 1022) count = 253;

    ThreadPool pool(64); 

    if (ni.has_raw) {
        auto found = arp_scan_raw(ni, timeout_ms);
        for (auto& [ip, mac] : found) {
            pool.enqueue([ip, mac, timeout_ms]() {
                string ip_str = ip_to_str(ip);
                string mac_str = mac_to_str(mac.data());
                string vendor  = get_vendor(mac_str);
                long ms = ping_linux(ip, min(timeout_ms / 4, 800));
                lock_guard<mutex> lg(results_mutex);
                results.push_back({ip_str, mac_str, vendor, max(0L, ms)});
            });
        }
    } else {
        for (uint32_t h = 1; h <= count; h++) {
            uint32_t tip = htonl(ntohl(netw) + h);
            int s = socket(AF_INET, SOCK_DGRAM, 0);
            if (s >= 0) {
                struct sockaddr_in dst{};
                dst.sin_family = AF_INET;
                dst.sin_port = htons(7);
                dst.sin_addr.s_addr = tip;
                connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
                char buf[1]{}; send(s, buf, 1, 0); close(s);
            }
        }
        usleep(100000); 

        for (uint32_t h = 1; h <= count; h++) {
            uint32_t tip = htonl(ntohl(netw) + h);
            pool.enqueue([tip, timeout_ms]() { scan_host_linux(tip, timeout_ms); });
        }
    }
}
#endif 
//  WINDOWS
#ifdef _WIN32

struct WinNetInfo {
    uint32_t local_ip;
    uint32_t netmask;
};

bool get_net_info_win(WinNetInfo& ni) {
    ULONG buf_len = 15000;
    vector<char> buf(buf_len);
    auto* adapters = reinterpret_cast<IP_ADAPTER_INFO*>(buf.data());
    if (GetAdaptersInfo(adapters, &buf_len) != ERROR_SUCCESS) return false;

    for (auto* a = adapters; a; a = a->Next) {
        string ip_str = a->IpAddressList.IpAddress.String;
        string nm_str = a->IpAddressList.IpMask.String;
        if (ip_str == "0.0.0.0") continue;
        ni.local_ip = inet_addr(ip_str.c_str());
        ni.netmask  = inet_addr(nm_str.c_str());
        return true;
    }
    return false;
}

long ping_windows(uint32_t ip, int timeout_ms) {
    HANDLE hIcmp = IcmpCreateFile();
    if (hIcmp == INVALID_HANDLE_VALUE) return -1;

    char send_buf[32] = "ORION_NETSCAN";
    DWORD reply_size = sizeof(ICMP_ECHO_REPLY) + sizeof(send_buf) + 8;
    vector<char> reply(reply_size);
    IPAddr dst = ip;
    DWORD ret = IcmpSendEcho(hIcmp, dst, send_buf, sizeof(send_buf), nullptr, reply.data(), reply_size, (DWORD)timeout_ms);
    IcmpCloseHandle(hIcmp);
    if (!ret) return -1;
    auto* r = reinterpret_cast<ICMP_ECHO_REPLY*>(reply.data());
    if (r->Status != IP_SUCCESS) return -1;
    return (long)r->RoundTripTime;
}

void scan_host_win(uint32_t ip_n, int timeout_ms) {
    ULONG mac_buf[2] = {};
    ULONG mac_len = 6;
    IPAddr dst = ip_n;
    DWORD ret = SendARP(dst, 0, mac_buf, &mac_len);
    if (ret != NO_ERROR || mac_len == 0) return; 

    auto* mb = reinterpret_cast<BYTE*>(mac_buf);
    char mac_cstr[18];
    snprintf(mac_cstr, sizeof(mac_cstr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mb[0], mb[1], mb[2], mb[3], mb[4], mb[5]);
    
    string mac_str = mac_cstr;
    string vendor  = get_vendor(mac_str);
    string ip_str  = ip_to_str(ip_n);
    long ms = ping_windows(ip_n, min(timeout_ms / 4, 800));

    lock_guard<mutex> lg(results_mutex);
    results.push_back({ip_str, mac_str, vendor, max(0L, ms)});
}

void run_windows(int timeout_ms) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    WinNetInfo ni;
    if (!get_net_info_win(ni)) {
        cerr << "ERR: Не найден сетевой адаптер\n";
        return;
    }

    uint32_t netw     = ni.local_ip & ni.netmask;
    uint32_t hostmask = ~ni.netmask;
    uint32_t count    = ntohl(hostmask) - 1;
    if (count > 1022) count = 253;

    ThreadPool pool(64); 

    for (uint32_t h = 1; h <= count; h++) {
        uint32_t tip = htonl(ntohl(netw) + h);
        pool.enqueue([tip, timeout_ms]() { scan_host_win(tip, timeout_ms); });
    }
    
    WSACleanup();
}
#endif // _WIN32

// ─── MAIN ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    int timeout_ms = 2000;
    if (argc > 1) {
        try { timeout_ms = max(500, min(10000, stoi(argv[1]))); }
        catch (...) {}
    }

#ifdef _WIN32
    run_windows(timeout_ms);
#else
    run_linux(timeout_ms);
#endif

    sort(results.begin(), results.end(), [](const HostResult& a, const HostResult& b) {
        return ntohl(inet_addr(a.ip.c_str())) < ntohl(inet_addr(b.ip.c_str()));
    });

    for (const auto& r : results) {
        string ping = (r.ping_ms >= 0) ? to_string(r.ping_ms) + " ms" : "?";
        cout << r.ip << "|" << r.mac << "|" << r.vendor << "|" << ping << "\n";
    }

    return 0;
}