namespace AuthServer.Services.Tcp;

public class TcpOptions
{
    public string Host { get; set; } = "127.0.0.1";
    public int Port { get; set; }
    public int PoolSeize { get; set; } = 10;
}
