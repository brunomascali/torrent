set(TARGET_NAME download)

add_library(${TARGET_NAME} STATIC
  download_session.cxx
  piece_picker.cxx
  storage.cxx
  peer_id.cxx
  tracker/announce.cxx
  tracker/tracker_client.cxx
  peer/peer.cxx
  peer/peer_connection.cxx
  peer/wire/error.cxx
  peer/wire/handshake.cxx
  peer/wire/message.cxx
  peer/wire/stream.cxx
)

target_include_directories(${TARGET_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}/src/modules
  ${CMAKE_SOURCE_DIR}/src/external
  /usr/include/boost
)

target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)

target_compile_definitions(${TARGET_NAME} PUBLIC
  CPPHTTPLIB_OPENSSL_SUPPORT
)

find_package(OpenSSL REQUIRED)

target_link_libraries(${TARGET_NAME} PUBLIC
  OpenSSL::SSL
  OpenSSL::Crypto
  metainfo
  error
  bencode
  io
  logger
)
