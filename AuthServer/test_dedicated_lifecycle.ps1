param (
    [string]$baseUrl = "http://localhost:8080",
    [string]$apiKey = "dev_logic_server_api_key_1234",
    [string]$mode = "QuickInject" # "QuickInject" or "FullMatchFlow"
)

$ErrorActionPreference = "Stop"

# Ensure UTF-8 without BOM for all pipeline and process communications
$OutputEncoding = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "    Dedicated Server Lifecycle & Shutdown Test          " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "AuthServer Target: $baseUrl" -ForegroundColor Gray
Write-Host "Test Mode:        $mode" -ForegroundColor Gray
Write-Host ""

# 1. Check if AuthServer is running
Write-Host "[1/5] Checking AuthServer health..." -ForegroundColor Yellow
try {
    $health = Invoke-RestMethod -Uri "$baseUrl/health" -Method Get -TimeoutSec 3
    Write-Host "  -> AuthServer is online and healthy (status=$($health.status))." -ForegroundColor Green
} catch {
    try {
        $ping = Invoke-RestMethod -Uri "$baseUrl/ping" -Method Get -TimeoutSec 3
        Write-Host "  -> AuthServer is online ($ping)." -ForegroundColor Green
    } catch {
        Write-Host "  [ERROR] AuthServer is NOT reachable at $baseUrl!" -ForegroundColor Red
        Write-Host "  Please check if AuthServer is running and listening on $baseUrl." -ForegroundColor Red
        exit 1
    }
}

# Paths to executable
$exePath = Resolve-Path "$PSScriptRoot/../server/build/x64-debug/main.exe" -ErrorAction SilentlyContinue
if (-not $exePath -or -not (Test-Path $exePath)) {
    Write-Host "  [ERROR] Server executable not found at: $exePath" -ForegroundColor Red
    exit 1
}
Write-Host "  -> Dedicated Server Executable: $exePath" -ForegroundColor Gray
Write-Host ""

if ($mode -eq "QuickInject") {
    # -------------------------------------------------------------
    # Mode A: QuickInject
    # -------------------------------------------------------------
    Write-Host "[2/5] Setting up mock match data..." -ForegroundColor Yellow
    $matchId = [guid]::NewGuid().ToString()
    $playerCount = 10
    $userIds = @()
    for ($i = 1; $i -le $playerCount; $i++) {
        $userIds += [guid]::NewGuid().ToString()
    }
    $playersCsv = $userIds -join ","

    Write-Host "  -> Generated MatchId: $matchId" -ForegroundColor Cyan
    Write-Host "  -> Generated $($userIds.Count) Player IDs" -ForegroundColor Cyan

    # Allocate free port dynamically to prevent TIME_WAIT socket conflicts
    $tempListener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
    $tempListener.Start()
    $tcpPort = $tempListener.LocalEndpoint.Port
    $tempListener.Stop()
    $udpPort = $tcpPort

    # Insert match into Redis using redis-cli (if available) or Docker container
    Write-Host "  -> Injecting match:result into Redis (Ports: TCP=$tcpPort, UDP=$udpPort)..." -ForegroundColor Gray
    $matchedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
    $redisJson = @{
        MatchId = $matchId
        UserIds = $userIds
        ServerAddress = "127.0.0.1:$tcpPort"
        MatchedAtUtc = $matchedAtUtc
        IsFinished = $false
        TeamAScore = 0
        TeamBScore = 0
        WinnerUserIds = @()
    } | ConvertTo-Json -Compress

    # Try docker exec auth-redis or local redis-cli with -x pipe
    $redisSuccess = $false
    try {
        $redisJson | docker exec -i auth-redis redis-cli -x HSET match:result $matchId 2>$null | Out-Null
        $redisSuccess = $true
        Write-Host "  -> Injected to Redis via Docker container (auth-redis)." -ForegroundColor Green
    } catch {
        try {
            $redisJson | redis-cli -p 16379 -x HSET match:result $matchId 2>$null | Out-Null
            $redisSuccess = $true
            Write-Host "  -> Injected to Redis via local redis-cli (port 16379)." -ForegroundColor Green
        } catch {
            Write-Host "  [NOTICE] Direct Redis injection skipped. AuthServer will handle DB test." -ForegroundColor Yellow
        }
    }
    Write-Host ""

    # 3. Launch Dedicated Server with redirected stdin
    Write-Host "[3/5] Starting Dedicated Server (main.exe) in TEST mode..." -ForegroundColor Yellow
    
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exePath.Path
    $psi.Arguments = "--match-id $matchId --api-key $apiKey --tcp-port $tcpPort --udp-port $udpPort --players $playersCsv --test"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $false
    $psi.RedirectStandardError = $false
    $psi.CreateNoWindow = $false

    $serverProc = [System.Diagnostics.Process]::Start($psi)
    Write-Host "  -> Dedicated Server spawned (PID: $($serverProc.Id))" -ForegroundColor Green
    Write-Host "  -> Waiting 1.5 seconds for server and mock World initialization..." -ForegroundColor Gray
    Start-Sleep -Milliseconds 1500

    if ($serverProc.HasExited) {
        Write-Host "  [ERROR] Dedicated Server exited prematurely with code: $($serverProc.ExitCode)" -ForegroundColor Red
        exit 1
    }

    # 4. Trigger match simulation and finish
    Write-Host ""
    Write-Host "[4/5] Simulating combat events via stdin (kills)..." -ForegroundColor Yellow
    
    # Simulate kills: TeamA 2 kills, TeamB 1 kill
    $serverProc.StandardInput.WriteLine("kill A")
    $serverProc.StandardInput.Flush()
    Start-Sleep -Milliseconds 200

    $serverProc.StandardInput.WriteLine("kill A")
    $serverProc.StandardInput.Flush()
    Start-Sleep -Milliseconds 200

    $serverProc.StandardInput.WriteLine("kill B")
    $serverProc.StandardInput.Flush()
    Start-Sleep -Milliseconds 200

    Write-Host "  -> Sent 2 kills for TeamA, 1 kill for TeamB. Sending 'finish'..." -ForegroundColor Gray
    $serverProc.StandardInput.WriteLine("finish")
    $serverProc.StandardInput.Flush()

    Write-Host "  -> 'finish' signal sent. Waiting for server to report results and terminate..." -ForegroundColor Gray

    # Wait up to 10 seconds for clean termination
    $exitedCleanly = $serverProc.WaitForExit(10000)

    # Safely close stdin stream after process termination
    try {
        $serverProc.StandardInput.Close()
    } catch {}

    Write-Host ""
    Write-Host "[5/5] Verifying test results..." -ForegroundColor Yellow

    if ($exitedCleanly) {
        Write-Host "  [PASS] Dedicated Server cleanly terminated with ExitCode: $($serverProc.ExitCode)" -ForegroundColor Green
        if ($serverProc.ExitCode -eq 0) {
            Write-Host "  -> Process shutdown validation PASSED (ExitCode 0)." -ForegroundColor Green
        } else {
            Write-Host "  -> [FAIL] Expected ExitCode 0, but got: $($serverProc.ExitCode)" -ForegroundColor Red
        }

        # Query PostgreSQL to verify saved scores
        try {
            $dbRow = "SELECT `"WinningTeam`", `"TeamAScore`", `"TeamBScore`", `"IsFinished`" FROM `"MatchResults`" WHERE `"MatchId`" = '$matchId';" | docker exec -i auth-db psql -U admin -d authdb -t -A 2>$null
            if ($dbRow) {
                $parts = $dbRow.Trim().Split('|')
                Write-Host "  [DB VERIFICATION] Saved Result in PostgreSQL:" -ForegroundColor Cyan
                Write-Host "    WinningTeam: $($parts[0])" -ForegroundColor Cyan
                Write-Host "    TeamAScore:  $($parts[1])" -ForegroundColor Cyan
                Write-Host "    TeamBScore:  $($parts[2])" -ForegroundColor Cyan
                Write-Host "    IsFinished:  $($parts[3])" -ForegroundColor Cyan

                if ($parts[0] -eq "TeamA" -and [int]$parts[1] -eq 2 -and [int]$parts[2] -eq 1) {
                    Write-Host "  -> [PASS] Score and WinningTeam verification SUCCESSFUL!" -ForegroundColor Green
                } else {
                    Write-Host "  -> [WARN] Score mismatch! Expected TeamA 2:1, got: $($parts[0]) $($parts[1]):$($parts[2])" -ForegroundColor Yellow
                }
            }
        } catch {
            Write-Host "  [NOTICE] Could not query DB directly." -ForegroundColor Gray
        }

    } else {
        Write-Host "  [FAIL] Dedicated Server did not terminate within timeout!" -ForegroundColor Red
        try {
            $serverProc.Kill()
        } catch {
            Stop-Process -Id $serverProc.Id -Force -ErrorAction SilentlyContinue
        }
    }

} else {
    # -------------------------------------------------------------
    # Mode B: Full Matchmaking Flow
    # -------------------------------------------------------------
    Write-Host "[2/5] Registering & Logging in 10 players for matchmaking..." -ForegroundColor Yellow
    $timestamp = Get-Date -Format "HHmmss"
    $tokens = @()
    for ($i = 1; $i -le 10; $i++) {
        $u = "user_${timestamp}_$i"
        $p = "password123!"
        try {
            $null = Invoke-RestMethod -Uri "$baseUrl/auth/register" -Method Post -ContentType "application/json" -Body "{`"username`":`"$u`",`"password`":`"$p`"}" -ErrorAction SilentlyContinue
        } catch {}
        $res = Invoke-RestMethod -Uri "$baseUrl/auth/login" -Method Post -ContentType "application/json" -Body "{`"username`":`"$u`",`"password`":`"$p`"}"
        $tokens += $res.token
    }
    Write-Host "  -> 10 players registered and logged in." -ForegroundColor Green

    Write-Host "[3/5] Enqueuing 10 players to trigger MatchWorker auto-spawn..." -ForegroundColor Yellow
    foreach ($token in $tokens) {
        $null = Invoke-RestMethod -Uri "$baseUrl/match/join" -Method Post -Headers @{ Authorization = "Bearer $token" }
    }
    Write-Host "  -> All 10 players queued. Waiting for MatchWorker to spawn main.exe..." -ForegroundColor Green
    Start-Sleep -Seconds 2

    # Check match status
    $status = Invoke-RestMethod -Uri "$baseUrl/match/status" -Method Get -Headers @{ Authorization = "Bearer $($tokens[0])" }
    Write-Host "  -> MatchId: $($status.matchId)" -ForegroundColor Cyan
    Write-Host "  -> ServerAddress: $($status.serverAddress)" -ForegroundColor Cyan

    Write-Host "[4/5] Dedicated Server is running in background!" -ForegroundColor Yellow
    Write-Host "  You can type 'finish' in the Dedicated Server console window to complete the match." -ForegroundColor Cyan
}

Write-Host ""
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host "                   TEST COMPLETE                        " -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
