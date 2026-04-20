namespace AuthServer.Services;

public class GlobalFields
{
    public GlobalFields() { }
    public int PlayerCount { get; private set; } = 10;
    public int TimeOutSec { get; private set; } = 20;
}