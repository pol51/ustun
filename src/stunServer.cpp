#include "stunServer.hpp"

#include <random>
#include <spdlog/spdlog.h>


using boost::asio::ip::udp;

constexpr uint32_t MAGIC_COOKIE         = 0x2112A442;
constexpr uint16_t BINDING_REQUEST      = 0x0001;
constexpr uint16_t BINDING_SUCCESS_RESP = 0x0101;
constexpr uint16_t XOR_MAPPED_ADDRESS   = 0x0020;

#pragma pack(push, 1)
struct StunHeader
{
  uint16_t type;
  uint16_t length;
  uint32_t cookie;
  uint8_t trans_id[12];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct XorMappedAddressHeader
{
  uint16_t type; // always 0x0020
  uint16_t length; // 8 (IPv4) or 20 (IPv6)
  uint8_t reserved; // always 0x00
  uint8_t family; // 0x01 = IPv4, 0x02 = IPv6
  uint16_t xport; // XORed port
};

struct XorMappedAddressIPv4
{
  XorMappedAddressHeader hdr;
  uint32_t xaddr; // XORed IPv4 address
};

struct XorMappedAddressIPv6
{
  XorMappedAddressHeader hdr;
  uint8_t xaddr[16]; // XORed IPv6 address
};
#pragma pack(pop)

StunServer::StunServer(boost::asio::io_context& io, const uint16_t port, uint32_t delay_ms,
    uint32_t max_delay_offset_ms)
    : _socket(io, udp::endpoint(udp::v4(), port))
    , _delay_ms(delay_ms)
    , _max_delay_offset_ms(max_delay_offset_ms)
{
  spdlog::info("STUN server listening on UDP port {}", port);
  startReceive();
}

void StunServer::stop()
{
  boost::system::error_code ec;
  _socket.close(ec);
  if (ec)
    spdlog::warn("Error while closing socket: {}", ec.message());
}

void StunServer::startReceive()
{
  _socket.async_receive_from(boost::asio::buffer(_buffer), _remote,
      [this](boost::system::error_code ec, std::size_t bytes) {
        if (!ec)
          handlePacket(bytes);
        startReceive();
      });
}

std::string StunServer::endpoint2str(const boost::asio::ip::udp::endpoint& remote)
{
  return fmt::format("{}:{}", remote.address().to_string(), remote.port());
}

void StunServer::handlePacket(const std::size_t bytes)
{
  if (bytes < sizeof(StunHeader))
    return;

  const auto* hdr   = reinterpret_cast<const StunHeader*>(_buffer.data());
  uint16_t msg_type = ntohs(hdr->type);
  uint32_t cookie   = ntohl(hdr->cookie);

  const auto remoteStr = endpoint2str(_remote);

  if (msg_type != BINDING_REQUEST || cookie != MAGIC_COOKIE)
  {
    spdlog::debug("Ignoring non-Binding or invalid STUN packet from {}", remoteStr);
    return;
  }

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<int32_t> dist(-_max_delay_offset_ms, _max_delay_offset_ms);
  auto delay_ms = _delay_ms + dist(gen);

  spdlog::info(
      "Received Binding Request from {}, scheduling response in {}ms", remoteStr, delay_ms);

  std::vector<uint8_t> attrs;
  buildXorMappedAttr(attrs, _remote, hdr->trans_id);

  StunHeader resp_hdr {};
  resp_hdr.type   = htons(BINDING_SUCCESS_RESP);
  resp_hdr.length = htons(attrs.size());
  resp_hdr.cookie = htonl(MAGIC_COOKIE);
  std::memcpy(resp_hdr.trans_id, hdr->trans_id, 12);

  auto resp = std::make_shared<std::vector<uint8_t>>(sizeof(resp_hdr) + attrs.size());
  std::memcpy(resp->data(), &resp_hdr, sizeof(resp_hdr));
  std::memcpy(resp->data() + sizeof(resp_hdr), attrs.data(), attrs.size());

  auto remote = std::make_shared<boost::asio::ip::udp::endpoint>(_remote);

  auto timer = std::make_shared<boost::asio::steady_timer>(_socket.get_executor());
  timer->expires_after(std::chrono::milliseconds(delay_ms));
  timer->async_wait(
      [this, timer, resp, remote, remoteStr = std::move(remoteStr)](boost::system::error_code ec) {
        if (ec)
        {
          spdlog::warn("Timer error for {}: {}", remoteStr, ec.message());
          return;
        }

        _socket.async_send_to(boost::asio::buffer(*resp), *remote,
            [remoteStr = std::move(remoteStr)](boost::system::error_code send_ec, std::size_t) {
              if (send_ec)
                spdlog::warn("Failed to send response to {}: {}", remoteStr, send_ec.message());
              else
                spdlog::debug("Sent delayed Binding Success to {}", remoteStr);
            });
      });
}

void StunServer::buildXorMappedAttr(
    std::vector<uint8_t>& out, const udp::endpoint& src, const uint8_t trans_id[12])
{
  const auto addr   = src.address();
  uint16_t port_xor = src.port() ^ (MAGIC_COOKIE >> 16);

  if (addr.is_v4())
  {
    uint32_t ip = addr.to_v4().to_uint() ^ MAGIC_COOKIE;

    XorMappedAddressIPv4 attr {};
    attr.hdr.type     = htons(XOR_MAPPED_ADDRESS);
    attr.hdr.length   = htons(8);
    attr.hdr.reserved = 0x00;
    attr.hdr.family   = 0x01;
    attr.hdr.xport    = htons(port_xor);
    attr.xaddr        = htonl(ip);

    const uint8_t* p = reinterpret_cast<const uint8_t*>(&attr);
    out.insert(out.end(), p, p + sizeof(attr));
  }
  else if (addr.is_v6())
  {
    auto ip6 = addr.to_v6().to_bytes();

    // XOR the first 4 bytes with the magic cookie,
    // and the rest with the transaction ID (RFC 5389 §15.2)
    uint8_t xor_mask[16];
    std::memcpy(xor_mask, &MAGIC_COOKIE, 4);
    std::memcpy(xor_mask + 4, trans_id, 12);

    for (int i = 0; i < 16; i++)
      ip6[i] ^= xor_mask[i];

    XorMappedAddressIPv6 attr {};
    attr.hdr.type     = htons(XOR_MAPPED_ADDRESS);
    attr.hdr.length   = htons(20);
    attr.hdr.reserved = 0x00;
    attr.hdr.family   = 0x02;
    attr.hdr.xport    = htons(port_xor);
    std::memcpy(attr.xaddr, ip6.data(), 16);

    const uint8_t* p = reinterpret_cast<const uint8_t*>(&attr);
    out.insert(out.end(), p, p + sizeof(attr));
  }
}
