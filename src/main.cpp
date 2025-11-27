#include "stunServer.hpp"

#include <spdlog/spdlog.h>

#include <boost/asio/signal_set.hpp>

int main(int argc, char* argv[])
{
  try
  {
    uint16_t port = (argc > 1) ? static_cast<uint16_t>(std::stoi(argv[1])) : 3478;

    spdlog::set_level(spdlog::level::debug);

    uint32_t delay_ms =
        getenv("DELAY_MS") ? static_cast<uint16_t>(std::stoi(getenv("DELAY_MS"))) : 0;

    uint32_t max_delay_offset_ms = getenv("MAX_DELAY_OFFSET_MS")
        ? static_cast<uint16_t>(std::stoi(getenv("MAX_DELAY_OFFSET_MS")))
        : 0;

    boost::asio::io_context io;
    StunServer server(io, port, delay_ms, max_delay_offset_ms);

    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int signal) {
      spdlog::info("Received signal {}, stopping server...", signal);
      server.stop();
      io.stop();
    });

    spdlog::info("Server ready. Press Ctrl+C to stop.");
    io.run();
    spdlog::info("Server stopped.");
  }
  catch (const std::exception& e)
  {
    spdlog::error("Fatal: {}", e.what());
    return 1;
  }
}
