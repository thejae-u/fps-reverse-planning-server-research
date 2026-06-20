namespace AuthServer.Services.Tcp;

public class TcpOptions
{
    public string Host { get; set; } = "127.0.0.1";
    public int ListenPort { get; set; } = 9002;
    public int ReqPort { get; set; } = 9001;
    public int PoolSize { get; set; } = 10;
}
