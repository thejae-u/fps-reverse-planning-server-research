using System.Collections;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Threading.Tasks;
using UnityEngine;

public class NetworkManager : Singleton<NetworkManager>
{
    [SerializeField]
    private string _serverIp = "127.0.0.1";
    [SerializeField]
    private uint _serverPort = 54800;

    private TcpClient _serverTcp;
    private UdpClient _serverUdp;

    private IPEndPoint _ep;

    private bool _isConnected;

    private void Awake()
    {
        _isConnected = false;
    }

    private void Start()
    {
    }

    private void Update()
    {
    }

    private void ConnectToServer()
    {
    }

    private async Task ConnectAsync()
    {
        await _serverTcp.ConnectAsync(IPAddress.Parse(_serverIp), (int)_serverPort);
        await Task.Yield();
    }
}
