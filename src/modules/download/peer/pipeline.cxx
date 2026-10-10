#include <download/peer/pipeline.hxx>

namespace download::peer {

Pipeline::Pipeline(DownloadManager& download_manager)
  : m_download_manager(download_manager) {}

asio::awaitable<void> Pipeline::fill(const std::vector<bool> bitfield) {

  m_download_manager.reserve_piece(bitfield);

  co_return;
}

}
