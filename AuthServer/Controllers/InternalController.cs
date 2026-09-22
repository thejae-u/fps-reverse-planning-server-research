using AuthServer.Dtos;
using AuthServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace AuthServer.Controllers;

[ApiController]
[Authorize(Roles = "Admin,Internal")]
[Route("internal")]
public class InternalController : ControllerBase
{
    private readonly MatchService _matchService;
    private readonly IConfiguration _config;
    private readonly ILogger<InternalController> _logger;

    public InternalController(MatchService matchService, IConfiguration config, ILogger<InternalController> logger)
    {
        _matchService = matchService;
        _config = config;
        _logger = logger;
    }
    
    [HttpGet("all")]
    public ActionResult<List<DedicatedServerInfo>> GetMatchListAll()
    {
        return NoContent();
    }

    [HttpGet("get-match")]
    public ActionResult<DedicatedServerInfo> GetMatch(string matchId)
    {
        return NoContent();
    }

    [HttpPost("finish")]
    public async Task<IActionResult> FinishGame([FromBody] GameResultReportDto report)
    {
        var tokenMatchId = User.FindFirst("match_id")?.Value;
        if (!string.IsNullOrEmpty(tokenMatchId) && tokenMatchId != report.MatchId)
        {
            _logger.LogWarning("[FinishGame] Token match_id ({TokenMatchId}) does not match reported MatchId({ReportMatchId})",
                tokenMatchId, report.MatchId);
            return Forbid();
        }
        
        var result = await _matchService.FinishMatchAsync(report);

        if (!result)
        {
            _logger.LogWarning("[FinishGame] Match not found or already finished: {MatchId}", report.MatchId);
            return NotFound(new { message = "Match not found or already finished" });
        }

        return Ok(new { message = "Game result successfully processed" });
    }
}