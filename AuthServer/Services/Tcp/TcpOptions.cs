namespace AuthServer.Services.Tcp;

public class TcpOptions
{
    public string Host { get; set; } = "127.0.0.1";
    public int Port { get; set; } = 9000;
    public int PoolSize { get; set; } = 10;
}
