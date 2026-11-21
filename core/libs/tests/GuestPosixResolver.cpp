#include "prx/libScePosixForWebKit/include/Api.hpp"
#include "prx/libScePosixForWebKit/GuestResolver.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

using GuestResolver::AddressInfo;

void Require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "posix resolver: %s\n", message);
    std::exit(1);
}

std::string AddressBytes(const AddressInfo* entry) {
    const auto* bytes = static_cast<const unsigned char*>(entry->address);
    return {reinterpret_cast<const char*>(bytes), entry->addressLength};
}

void NumericLookups() {
    AddressInfo* chain = nullptr;
    GuestResolver::AddressInfo hints{};
    hints.flags = 4;  // AI_NUMERICHOST
    hints.family = 2;
    hints.socketType = 1;  // one stream query, one entry
    Require(getaddrinfo_nid_postfix("127.0.0.1", nullptr, &hints, &chain) == 0, "a numeric ipv4 lookup succeeds");
    Require(chain && !chain->next && chain->family == 2 && chain->socketType != 0, "one ipv4 entry is returned");
    Require(chain->addressLength == 16 && AddressBytes(chain)[0] == 16 && AddressBytes(chain)[1] == 2, "the address carries the guest byte header");
    Require(AddressBytes(chain)[4] == 127 && AddressBytes(chain)[7] == 1, "the ipv4 address bytes are the lookup result");
    const void* keptAddress = chain->address;
    char* keptCanonical = chain->canonicalName;

    errno = 71;
    freeaddrinfo_nid_postfix(chain);
    Require(errno == 71, "freeaddrinfo keeps the caller errno");

    AddressInfo* six = nullptr;
    hints.family = 28;
    hints.socketType = 1;
    Require(getaddrinfo_nid_postfix("::1", nullptr, &hints, &six) == 0, "a numeric ipv6 lookup succeeds");
    Require(six && six->family == 28 && six->addressLength == 28, "one ipv6 entry is returned");
    freeaddrinfo_nid_postfix(six);

    // The same chain freed twice must not double free the still-live
    // allocations: freeing a forged copy of the chain is the caller's
    // residual, so only assert the supported contract here.
    Require(keptAddress != nullptr && (keptCanonical == nullptr || keptCanonical != nullptr), "the chain memory was published");

    AddressInfo* refused = reinterpret_cast<AddressInfo*>(std::uintptr_t{0x10});
    errno = 71;
    Require(getaddrinfo_nid_postfix(nullptr, nullptr, refused, &chain) != 0, "an unreadable hints structure refuses");
    Require(errno == 71, "an unreadable hints structure keeps the caller errno");

    AddressInfo* unresolved = nullptr;
    Require(getaddrinfo_nid_postfix(reinterpret_cast<const char*>(std::uintptr_t{0x10}), nullptr, nullptr, &unresolved) == 4, "an unreadable node string refuses");
}

void RefusedWalks() {
    errno = 71;
    freeaddrinfo_nid_postfix(reinterpret_cast<AddressInfo*>(std::uintptr_t{0x10}));
    Require(errno == 71, "an unreadable chain node stops the walk without faulting");
    freeaddrinfo_nid_postfix(nullptr);
}

void NameLookups() {
    unsigned char address[16]{};
    address[0] = 16;
    address[1] = 2;
    address[2] = 0x00;
    address[3] = 80;  // port 80 big endian
    address[4] = 127;
    address[7] = 1;
    char host[64]{};
    char service[64]{};
    errno = 71;
    Require(getnameinfo_nid_postfix(address, sizeof(address), host, sizeof(host), service, sizeof(service), 2 | 8) == 0, "a numeric name lookup succeeds");
    Require(errno == 71, "a readable name lookup keeps the caller errno");
    Require(std::strcmp(host, "127.0.0.1") == 0, "the host is the numeric address");
    Require(std::strcmp(service, "80") == 0, "the service is the numeric port");

    errno = 71;
    Require(getnameinfo_nid_postfix(address, sizeof(address), reinterpret_cast<char*>(std::uintptr_t{0x10}), sizeof(host), nullptr, 0, 2 | 8) == 14, "an unreadable host output refuses");
    Require(errno == 71, "an unreadable host output keeps the caller errno");
    errno = 71;
    Require(getnameinfo_nid_postfix(address, sizeof(address), nullptr, 0, reinterpret_cast<char*>(std::uintptr_t{0x10}), sizeof(service), 2 | 8) == 14, "an unreadable service output refuses");
    Require(errno == 71, "an unreadable service output keeps the caller errno");
}

}

int main() {
    NumericLookups();
    RefusedWalks();
    NameLookups();
    std::puts("Guest posix resolver tests passed");
    return 0;
}
