#include "download/download_manager.hxx"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <atomic>

namespace download::peer {

  class Pipeline {
    public:
      Pipeline(DownloadManager& download_manager);

      asio::awaitable<void> fill(const std::vector<bool> bitfield);

    private:
      struct IncomingPiece {
        uint32_t piece_length;
        std::vector<std::byte> data;
        std::vector<bool> requested;
        std::vector<bool> received;
      };

      DownloadManager& m_download_manager;
      std::map<uint32_t, IncomingPiece> m_incoming_pieces;

      static constexpr uint32_t MAX_PIECES_PER_PEER = 16;
      static constexpr uint32_t MAX_PARALLEL = 128;
  };
}
