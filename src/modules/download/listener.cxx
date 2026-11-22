#include <download/listener.hxx>

#include <boost/asio.hpp>
#include <format>
#include <logger/logger.hxx>
#include <memory>
#include <optional>

namespace download {

namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

std::optional<tcp::acceptor> bind_acceptor(asio::any_io_executor executor,
                                           const tcp::endpoint &endpoint) {
  tcp::acceptor acceptor(executor);
  boost::system::error_code ec;
  acceptor.open(endpoint.protocol(), ec);
  if (ec) {
    return std::nullopt;
  }
  acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
  if (endpoint.address().is_v6()) {
    acceptor.set_option(asio::ip::v6_only(true), ec);
  }
  acceptor.bind(endpoint, ec);
  if (ec) {
    return std::nullopt;
  }
  acceptor.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    return std::nullopt;
  }
  return acceptor;
}

asio::awaitable<void> accept_loop(tcp::acceptor acceptor,
                                  std::shared_ptr<Listener::AcceptHandler> on_accept) {
  while (true) {
    auto [ec, socket] = co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
    if (ec == asio::error::operation_aborted) {
      co_return;
    }
    if (ec) {
      logger::warn("Accept failed: {}", ec.message());
      asio::steady_timer pause(acceptor.get_executor(), std::chrono::milliseconds(200));
      co_await pause.async_wait(asio::as_tuple(asio::use_awaitable));
      continue;
    }
    (*on_accept)(io::TcpConnection::adopt(std::move(socket)));
  }
}

}

std::expected<Listener, std::string>
Listener::open(asio::any_io_executor executor, std::uint16_t first_port,
               std::uint16_t last_port) {
  for (std::uint32_t port = first_port; port <= last_port; ++port) {
    auto v4 = bind_acceptor(executor, tcp::endpoint(tcp::v4(), static_cast<std::uint16_t>(port)));
    if (!v4) {
      continue;
    }
    const std::uint16_t bound = v4->local_endpoint().port();

    std::vector<tcp::acceptor> acceptors;
    acceptors.push_back(std::move(*v4));
    if (auto v6 = bind_acceptor(executor, tcp::endpoint(tcp::v6(), bound))) {
      acceptors.push_back(std::move(*v6));
    }
    return Listener(std::move(acceptors), bound);
  }
  return std::unexpected(std::format("no free port in {}-{}", first_port, last_port));
}

void Listener::start(Listener listener, AcceptHandler on_accept) {
  auto handler = std::make_shared<AcceptHandler>(std::move(on_accept));
  for (auto &acceptor : listener.m_acceptors) {
    auto executor = acceptor.get_executor();
    asio::co_spawn(executor, accept_loop(std::move(acceptor), handler),
                   [](std::exception_ptr e) {
                     if (!e) {
                       return;
                     }
                     try {
                       std::rethrow_exception(e);
                     } catch (const std::exception &ex) {
                       logger::error("Listener stopped: {}", ex.what());
                     }
                   });
  }
}

}
