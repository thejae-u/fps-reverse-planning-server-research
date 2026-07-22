using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using AuthServer.Dtos;
using Microsoft.AspNetCore.Identity;

namespace AuthServer.Services;

public class DedicatedServerInfo
{
    public string MatchId { get; set; } = string.Empty;
    public int TcpPort { get; set; }
    public int UdpPort { get; set; }
    public Process Process { get; set; } = null!;
}

public interface IDedicatedServerSpawner
{
    Task<DedicatedServerInfo?> SpawnServerAsync(string matchId, List<string> userIds);
}

public class DedicatedServerSpawner : IDedicatedServerSpawner
{
    private readonly IConfiguration _config;
    private readonly ILogger<DedicatedServerSpawner> _logger;
    private readonly MatchService _matchService;

    public DedicatedServerSpawner(IConfiguration config, ILogger<DedicatedServerSpawner> logger, MatchService matchService)
    {
        _config = config;
        _logger = logger;
        _matchService = matchService;
    }
    
    public Task<DedicatedServerInfo?> SpawnServerAsync(string matchId, List<string> userIds)
    {
        try
        {
            int tcpPort = GetFreeBothTcpAndUdpPort();
            int udpPort = GetFreeBothTcpAndUdpPort();

            // OS에 따라 맞는 경로 설정
            string executablePath = ResolveExecutablePath();
            string idCsv = string.Join(",", userIds);

            // CLI 인자 구성
            var startInfo = new ProcessStartInfo
            {
                FileName = executablePath,
                Arguments = $"--match-id {matchId} --tcp-port {tcpPort} --udp-port {udpPort} --players {idCsv}",
                UseShellExecute = false,
                CreateNoWindow = true
            };

            var process = new Process
            {
                StartInfo = startInfo,
                EnableRaisingEvents = true
            };

            process.Exited += async (sender, args) =>
            {
                _logger.LogInformation(
                    $"[DedicatedServer] Match {matchId} process exited with code {process.ExitCode}");
                if (process.ExitCode != 0)
                {
                    _logger.LogError($"[DedicatedServer] Match {matchId} CRASHED! Recovering match state...");
                    await _matchService.FinishMatchAsync(matchId, TeamSide.None, userIds);
                }
            };

            bool started = process.Start();
            if (!started)
            {
                _logger.LogError($"Failed to start process: {executablePath}");
                return Task.FromResult<DedicatedServerInfo?>(null);
            }

            _logger.LogInformation(
                $"[DedicatedServer] Spawned process (PID: {process.Id}) for Match {matchId} on TCP: {tcpPort}, UDP: {udpPort}");

            return Task.FromResult<DedicatedServerInfo?>(new DedicatedServerInfo
            {
                MatchId = matchId,
                TcpPort = tcpPort,
                UdpPort = udpPort,
                Process = process
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, $"Exception occurred while spawning dedicated server for match {matchId}");
            return Task.FromResult<DedicatedServerInfo?>(null);
        }
    }

    private string ResolveExecutablePath()
    {
        string osKey = RuntimeInformation.IsOSPlatform(OSPlatform.Windows) ? "Windows" :
            RuntimeInformation.IsOSPlatform(OSPlatform.OSX) ? "OSX" : "Linux";

        string? configuredPath = _config[$"LogicServer:ExecutablePaths:{osKey}"];
        if (!string.IsNullOrEmpty(configuredPath))
            return Path.GetFullPath(configuredPath);
        
        bool isWindows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);
        string binaryName = isWindows ? "main.exe" : "main";

        string basePath = AppContext.BaseDirectory;

        string defaultPath = isWindows
            ? Path.Combine(basePath, "..", "..", "..", "..", "server", "build", "Debug", binaryName)
            : Path.Combine(basePath, "..", "..", "..", "..", "server", "build", binaryName);

        return defaultPath;
    }

    private int GetFreeBothTcpAndUdpPort()
    {
        const int maxTries = 10;

        for (int i = 0; i < maxTries; ++i)
        {
            using var tcpSocket = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            tcpSocket.Bind(new IPEndPoint(IPAddress.Any, 0));
            int allocatedPort = ((IPEndPoint)tcpSocket.LocalEndPoint!).Port;

            try
            {
                using var udpSocket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
                udpSocket.Bind(new IPEndPoint(IPAddress.Any, allocatedPort));

                return allocatedPort;
            }
            catch (SocketException) 
            { 
                // 같은 포트 배정 실패 시 
                continue;
            }
        }

        throw new Exception("Failed to allocate port");
    }
}