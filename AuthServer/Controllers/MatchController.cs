using System.Security.Claims;
using AuthServer.Data;
using AuthServer.Dtos;
using AuthServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace AuthServer.Controllers;

[ApiController]
[Authorize]
[Route("match")]
public class MatchController : ControllerBase
{
    private readonly MatchService _matchService;
    private readonly ApplicationDbContext _dbContext;

    public MatchController(MatchService matchService)
    {
        _matchService = matchService;
    }

    [HttpPost("join")]
    public async Task<IActionResult> Join()
    {
        var userId = User.FindFirstValue(ClaimTypes.NameIdentifier);
        var username = User.FindFirstValue(ClaimTypes.Name);

        if (string.IsNullOrEmpty(userId) || string.IsNullOrEmpty(username))
        {
            return Unauthorized(new ErrorResponse
            {
                Code = "UNAUTHORIZED",
                Message = "유효한 사용자 정보가 없습니다."
            });
        }

        var result = await _matchService.Join(userId, username);

        if (!result.IsSuccess)
        {
            return Conflict(new ErrorResponse
            {
                Code = result.ErrorCode,
                Message = result.ErrorMessage
            });
        }

        if (result.Data is null)
        {
            return Problem("Internal match queue error", statusCode: 500);
        }

        return Ok(new MatchJoinResponse
        {
            UserId = result.Data.UserId,
            Username = result.Data.Username,
            Status = result.Data.Status.ToString(),
            JoinedAtUtc = result.Data.JoinedAtUtc
        });
    }

    [HttpPost("cancel")]
    public async Task<IActionResult> Cancel()
    {
        var userId = User.FindFirstValue(ClaimTypes.NameIdentifier);

        if (string.IsNullOrEmpty(userId))
        {
            return Unauthorized(new ErrorResponse
            {
                Code = "UNAUTHORIZED",
                Message = "유효한 사용자 정보가 없습니다."
            });
        }

        var result = await _matchService.Cancel(userId);

        if (!result.IsSuccess)
        {
            return Conflict(new ErrorResponse
            {
                Code = result.ErrorCode,
                Message = result.ErrorMessage
            });
        }

        if (result.Data is null)
        {
            return Problem("Internal match queue error", statusCode: 500);
        }

        var matchResult = await _matchService.GetMatchResultByUserId(result.Data.UserId);

        return Ok(new MatchStatusResponse
        {
            UserId = result.Data.UserId,
            Username = result.Data.Username,
            Status = result.Data.Status.ToString(),
            JoinedAtUtc = result.Data.JoinedAtUtc,
            MatchId = result.Data.MatchId,
            ServerAddress = matchResult?.ServerAddress
        });
    }

    [HttpGet("status")]
    public async Task<IActionResult> Status()
    {
        var userId = User.FindFirstValue(ClaimTypes.NameIdentifier);

        if (string.IsNullOrEmpty(userId))
        {
            return Unauthorized(new ErrorResponse
            {
                Code = "UNAUTHORIZED",
                Message = "유효한 사용자 정보가 없습니다."
            });
        }

        var entry = await _matchService.GetStatus(userId);

        if (entry is null)
        {
            return NotFound(new ErrorResponse
            {
                Code = "QUEUE_NOT_FOUND",
                Message = "매칭 큐 정보가 없습니다."
            });
        }

        var matchResult = await _matchService.GetMatchResultByUserId(userId);

        return Ok(new MatchStatusResponse
        {
            UserId = entry.UserId,
            Username = entry.Username,
            Status = entry.Status.ToString(),
            JoinedAtUtc = entry.JoinedAtUtc,
            MatchId = entry.MatchId,
            ServerAddress = matchResult?.ServerAddress
        });
    }

    [AllowAnonymous]
    [HttpPost("report-result")]
    public async Task<IActionResult> ReportResult([FromBody] GameResultReportDto report)
    {
        var serverKey = Environment.GetEnvironmentVariable("LOGIC_SERVER_API_KEY") ?? "default_secret_key";
        if(report.ApiKey != serverKey)
            return Unauthorized("Invalid API Key");

        var result = await _matchService.FinishMatchAsync(report.MatchId, report.WinnderId);
        if (!result)
            return NotFound(new { message = "Match not found or already finished" });

        return Ok(new { message = "Result processed successfully" });
    }
}