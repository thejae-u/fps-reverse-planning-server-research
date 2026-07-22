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

    public InternalController(MatchService matchService, IConfiguration config)
    {
        _matchService = matchService;
        _config = config;
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
    public async Task<ActionResult<GameResultReportDto>> FinishGame([FromBody] GameResultReportDto report)
    {
        var serverKey = _config["LogicServer:ApiKey"] ?? "default_secret_key";
        if (report.ApiKey != serverKey)
            return Unauthorized("Invalid Internal ApiKey");

        var result = await _matchService.FinishMatchAsync(
            report.MatchId,
            report.WinningTeam,
            report.WinnerUserIds
        );

        if (!result)
            return NotFound(new { message = "Match not found or already finished" });
        return Ok(new { message = "Game result successfully processed" });
    }
}