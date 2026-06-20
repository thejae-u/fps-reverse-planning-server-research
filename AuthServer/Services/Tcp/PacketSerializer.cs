using System.Buffers.Binary;
using Google.Protobuf;
using AuthServer.Protos;

namespace AuthServer.Services.Tcp;

public static class PacketSerializer
{
    public const int HeaderSize = 2;

    public static byte[] Serialize(GamePacket packet)
    {
        byte[] body = packet.ToByteArray();
        ushort size = (ushort)body.Length;
        byte[] result = new byte[HeaderSize + size];

        // 2바이트 헤더를 Big Endian으로 기록
        BinaryPrimitives.WriteUInt16BigEndian(result.AsSpan(0, HeaderSize), size);

        Buffer.BlockCopy(body, 0, result, HeaderSize, body.Length);
        return result;
    }

    public static ushort DeserializeHeader(byte[] headerData)
    {
        if (headerData.Length < HeaderSize)
            return 0;

        // Big Endian 헤더에서 크기 추출
        return BinaryPrimitives.ReadUInt16BigEndian(headerData);
    }

    public static GamePacket Deserialize(byte[] bodyData)
    {
        return GamePacket.Parser.ParseFrom(bodyData);
    }
}
