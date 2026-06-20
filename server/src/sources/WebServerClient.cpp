#include "WebServerClient.hpp"

#include <regex>
#include "Internal.pb.h"

WebServerClient::WebServerClient(const size_t id, asio::io_context& io, const std::weak_ptr<ConnectionPool>& connectionPool) 

    : _connectionPool(connectionPool), _isInUse(false), _id(id), _sock(std::make_shared<asio::ip::tcp::socket>(io))
{
    UpdateActivityTime();
}

void WebServerClient::Rent()
{
    _isInUse = true;
    UpdateActivityTime();
}

void WebServerClient::Return()
{
    _isInUse = false;
}

void WebServerClient::OnDisconnected()
{
    _sock->close();
    _sock = nullptr;

    if(auto connectionPool = _connectionPool.lock())
    {
        connectionPool->NotifyExpired(shared_from_this());
    }
}

void WebServerClient::SendAsync(const Internal::GamePacket& packet)
{
    // 1. 바디 크기 계산
    std::uint16_t bodySize = static_cast<std::uint16_t>(packet.ByteSizeLong());

    // 2. 전체 패킷 버퍼 생성 (Header 2 bytes + Body)
    auto sendBuffer = std::make_shared<std::vector<uint8_t>>(HEADER_SIZE + bodySize);

    // 3. 헤더 처리: htons를 사용하여 네트워크 바이트 순서(Big Endian)로 변환
    std::uint16_t networkOrderSize = htons(bodySize);
    std::memcpy(sendBuffer->data(), &networkOrderSize, HEADER_SIZE);

    // 4. 본문 기록
    if (!packet.SerializeToArray(sendBuffer->data() + HEADER_SIZE, static_cast<int>(bodySize)))
    {
        spdlog::error("Failed to serialize GamePacket for sending (id: {})", _id);
        return;
    }

    // 5. 비동기 전송
    _sock->async_send(asio::buffer(*sendBuffer), [sendBuffer, weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
        if (auto self = weakSelf.lock())
        {
            if (ec)
            {
                if (ec != asio::error::operation_aborted)
                {
                    spdlog::error("internal connection send failed (id: {}): {}", self->GetId(), ec.message());
                    self->OnDisconnected();
                }
                return;
            }
            self->UpdateActivityTime();
        }
    });
}

void WebServerClient::ReceiveHeaderAsync()
{
    asio::async_read(*_sock, asio::buffer(_headerBuffer, HEADER_SIZE), [weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
        if (auto self = weakSelf.lock())
        {
            if (ec)
            {
                if (ec != asio::error::operation_aborted)
                {
                    spdlog::info("internal connection disconnected (id: {}): {}", self->GetId(), ec.message());
                    self->OnDisconnected();
                }
                return;
            }

            self->UpdateActivityTime();

            // 1. 헤더 파싱: ntohs를 사용하여 호스트 바이트 순서로 변환
            std::uint16_t bodySize;
            std::memcpy(&bodySize, self->_headerBuffer, HEADER_SIZE);
            bodySize = ntohs(bodySize);

            self->ReceiveBodyAsync(bodySize);
        }
    });
}

void WebServerClient::ReceiveBodyAsync(std::uint16_t bodySize)
{
    if (bodySize == 0)
    {
        ReceiveHeaderAsync();
        return;
    }

    auto bodyBuffer = std::make_shared<std::vector<uint8_t>>(bodySize);
    asio::async_read(*_sock, asio::buffer(*bodyBuffer), [bodyBuffer, weakSelf = weak_from_this()](const std::error_code& ec, std::size_t) {
        if (auto self = weakSelf.lock())
        {
            if (ec)
            {
                spdlog::info("internal connection body read failed (id: {}): {}", self->GetId(), ec.message());
                self->OnDisconnected();
                return;
            }

            self->UpdateActivityTime();

            // 1. 패킷 처리 로직을 비동기로 디스패치 (IO 스레드 점유 방지)
            asio::post(self->_sock->get_executor(), [self, bodyBuffer]() {
                Internal::GamePacket packet;
                if (packet.ParseFromArray(bodyBuffer->data(), static_cast<int>(bodyBuffer->size())))
                {
                    uint32_t seqId = packet.sequence_id();
                    switch (packet.payload_case())
                    {
                        case Internal::GamePacket::kMatchCreateReq:
                            self->HandleMatchCreateRequest(packet.match_create_req(), seqId);
                            break;
                        case Internal::GamePacket::kMatchCreateRes:
                            self->HandleMatchCreateResponse(packet.match_create_res(), seqId);
                            break;
                        default:
                            spdlog::warn("Unknown packet payload type: {} (id: {})", static_cast<int>(packet.payload_case()), self->GetId());
                            break;
                    }
                }
                else
                {
                    spdlog::error("Failed to parse GamePacket from body data: id={}", self->GetId());
                }
            });

            // 2. 즉시 다음 패킷 헤더 수신 대기
            self->ReceiveHeaderAsync();
        }
    });
}

void WebServerClient::HandleMatchCreateRequest(const Internal::MatchCreateRequest& req, uint32_t seqId)
{
    spdlog::info("HandleMatchCreateRequest: match_id={}, users_count={}, seqId={}", 
                 req.match_id(), req.users_size(), seqId);
    
    // TODO: 실제 룸 생성 로직 연동 (다음 작업)
}

void WebServerClient::HandleMatchCreateResponse(const Internal::MatchCreateResponse& res, uint32_t seqId)
{
    spdlog::info("HandleMatchCreateResponse: match_id={}, success={}, seqId={}", 
                 res.match_id(), res.success(), seqId);
}

bool WebServerClient::IsValid() const
{
    return _id != 0 && _sock && _sock->is_open();
}

void WebServerClient::ConnectAsync(const std::string& host, std::uint16_t port, std::function<void(const std::error_code&)> onConnected)
{
    auto endpoint = asio::ip::tcp::endpoint(asio::ip::make_address(host), port);
    _sock->async_connect(endpoint, [onConnected, weakSelf = weak_from_this()](const std::error_code& ec) {
        if(auto self = weakSelf.lock())
        {
            if(!ec)
            {
                spdlog::info("web server client: {} successfully connected to {}:{}", self->GetId(), self->_sock->remote_endpoint().address().to_string(), self->_sock->remote_endpoint().port());
                self->UpdateActivityTime();
                self->ReceiveHeaderAsync();
            }

            if(onConnected)
                onConnected(ec);
        }
    });
}