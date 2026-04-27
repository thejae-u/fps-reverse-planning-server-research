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

        byte[] sizeBytes = BitConverter.GetBytes(size);
        if (!BitConverter.IsLittleEndian) Array.Reverse(sizeBytes);

        Buffer.BlockCopy(sizeBytes, 0, result, 0, HeaderSize);
        Buffer.BlockCopy(body, 0, result, HeaderSize, body.Length);
        return result;
    }

    public static GamePacket Deserialize(byte[] data)
    {
        return GamePacket.Parser.ParseFrom(data);
    }
}
