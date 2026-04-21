using System.Security.Claims;
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

    public MatchController(MatchService matchService)
    {
        _matchService = matchService;
    }

    [HttpPost("join")]
    public IActionResult Join()
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

        var result = _matchService.Join(userId, username);

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
    public IActionResult Cancel()
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

        var result = _matchService.Cancel(userId);

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

        return Ok(new MatchStatusResponse
        {
            UserId = result.Data.UserId,
            Username = result.Data.Username,
            Status = result.Data.Status.ToString(),
            JoinedAtUtc = result.Data.JoinedAtUtc,
            MatchId = result.Data.MatchId,
            ServerAddress = _matchService.GetMatchResultByUserId(result.Data.UserId)?.ServerAddress
        });
    }

    [HttpGet("status")]
    public IActionResult Status()
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

        var entry = _matchService.GetStatus(userId);

        if (entry is null)
        {
            return NotFound(new ErrorResponse
            {
                Code = "QUEUE_NOT_FOUND",
                Message = "매칭 큐 정보가 없습니다."
            });
        }

        var matchResult = _matchService.GetMatchResultByUserId(userId);

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
}