using Google.Protobuf;

namespace AuthServer.Services.Tcp;

public static class PacketSerializer
{
    public const int HeaderSize = 2;

    public static byte[] Serialize<T>(T message) where T : IMessage
    {
        byte[] protobufData = message.ToByteArray();
        ushort payloadSize = (ushort)protobufData.Length;

        byte[] packet = new byte[HeaderSize + payloadSize];

        // size header
        byte[] sizeBytes = BitConverter.GetBytes(payloadSize);
        if (!BitConverter.IsLittleEndian) Array.Reverse(sizeBytes);
        Buffer.BlockCopy(sizeBytes, 0, packet, 0, 2);

        // protobuf binary
        Buffer.BlockCopy(protobufData, 0, packet, 2, protobufData.Length);

        return packet;
    }

    public static T Deserialize<T>(byte[] payload, MessageParser<T> parser) where T : IMessage<T>
    {
        return parser.ParseFrom(payload);
    }
}
