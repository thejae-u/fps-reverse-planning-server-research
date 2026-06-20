using System.Security.Claims;
using AuthServer.Dtos;
using AuthServer.Services;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;

namespace AuthServer.Controllers;

[ApiController]
[Route("auth")]
public class AuthController : ControllerBase
{
    private readonly UserService _userService;
    private readonly JwtTokenService _jwtTokenService;

    public AuthController(UserService userService, JwtTokenService jwtTokenService)
    {
        _userService = userService;
        _jwtTokenService = jwtTokenService;
    }

    [HttpPost("register")]
    public IActionResult Register([FromBody] RegisterRequest request)
    {
        var result = _userService.Register(request);

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
            return Problem("Request Success But Internal Error", statusCode: 500);
        }

        return Created($"/users/{result.Data.Id}", result.Data);
    }

    [HttpPost("login")]
    public IActionResult Login([FromBody] LoginRequest request)
    {
        var result = _userService.Login(request);

        if (!result.IsSuccess)
        {
            return Unauthorized(new ErrorResponse
            {
                Code = result.ErrorCode,
                Message = result.ErrorMessage
            });
        }

        if (result.Data is null)
        {
            return Problem("Request Success But Internal Error", statusCode: 500);
        }

        var token = _jwtTokenService.GenerateToken(result.Data);

        return Ok(new LoginResponse
        {
            UserId = result.Data.Id,
            Username = result.Data.Username,
            Token = token
        });
    }

    [Authorize]
    [HttpGet("me")]
    public IActionResult Me()
    {
        var userId = User.FindFirstValue(ClaimTypes.NameIdentifier);
        var username = User.FindFirstValue(ClaimTypes.Name);

        return Ok(new
        {
            UserId = userId,
            Username = username
        });
    }
}