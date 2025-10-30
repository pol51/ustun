#pragma once

#include <array>
#include <vector>

#include <boost/asio.hpp>

class StunServer {
  public:
    StunServer(boost::asio::io_context& io, const uint16_t port);
    
     void stop();

  private:
    void startReceive();
    void handlePacket(const std::size_t bytes);
    void buildXorMappedAttr(std::vector<uint8_t>& out, const boost::asio::ip::udp::endpoint& src,
        const uint8_t trans_id[12]);

    static std::string endpoint2str(const boost::asio::ip::udp::endpoint &remote);
  
  private:
    boost::asio::ip::udp::socket _socket;
    boost::asio::ip::udp::endpoint _remote;
    std::array<uint8_t, 1024> _buffer{};
};
