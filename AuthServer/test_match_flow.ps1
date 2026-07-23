param (
    [string]$baseUrl = "http://localhost:8080",
    [int]$playerCount = 10
)

Write-Host "========================================================" -ForegroundColor Yellow
Write-Host "   AuthServer & Dedicated C++ Server 10-Player Test     " -ForegroundColor Yellow
Write-Host "========================================================" -ForegroundColor Yellow
Write-Host "Target Server URL: ${baseUrl}" -ForegroundColor Gray
Write-Host "Required Player Count: ${playerCount}" -ForegroundColor Gray
Write-Host ""

# Unique Session ID for Users to avoid 409 Conflict from previous runs
$sessionId = Get-Date -Format "HHmmss"

# 1. Register & Login 10 Users with unique session IDs
Write-Host "[1/4] Registering & Logging in ${playerCount} test users (user_${sessionId}_1 ~ 10)..." -ForegroundColor Green
$tokens = @()

for ($i = 1; $i -le $playerCount; $i++) {
    $username = "user_${sessionId}_$i"
    $password = "password123!"

    try {
        $null = Invoke-RestMethod -Uri "${baseUrl}/auth/register" -Method Post -ContentType "application/json" -Body "{`"username`":`"$username`",`"password`":`"$password`"}" -ErrorAction SilentlyContinue
    } catch {}

    try {
        $res = Invoke-RestMethod -Uri "${baseUrl}/auth/login" -Method Post -ContentType "application/json" -Body "{`"username`":`"$username`",`"password`":`"$password`"}"
        $tokens += $res.token
        Write-Host "  -> Logged in: $username (Token: $($res.token.Substring(0, 15))...)" -ForegroundColor Cyan
    } catch {
        Write-Host "ERROR logging in $username at ${baseUrl}:" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
}

Write-Host ""

# 2. Join Match Queue for all 10 players
Write-Host "[2/4] Joining Match Queue for all ${playerCount} players..." -ForegroundColor Green
for ($i = 0; $i -lt $playerCount; $i++) {
    $userNum = $i + 1
    $token = $tokens[$i]

    try {
        $j = Invoke-RestMethod -Uri "${baseUrl}/match/join" -Method Post -Headers @{ Authorization = "Bearer $token" }
        Write-Host "  -> Player $userNum Joined: Status=$($j.status)"
    } catch {
        Write-Host "ERROR Player $userNum Joining Queue (409 Conflict or Error):" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "[3/4] Waiting 500ms for Dedicated Server Process Spawner..." -ForegroundColor Green
Start-Sleep -Milliseconds 500

# 3. Check Status using Player 1's token
Write-Host "[4/4] Checking Match Status & Server Connection Info (Player 1)..." -ForegroundColor Green
try {
    $status = Invoke-RestMethod -Uri "${baseUrl}/match/status" -Method Get -Headers @{ Authorization = "Bearer $($tokens[0])" }
    Write-Host ""
    Write-Host "================ MATCH RESULT ================" -ForegroundColor Green
    $status | Format-List
    Write-Host "==============================================" -ForegroundColor Green
} catch {
    Write-Host "ERROR Fetching Match Status:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
}
