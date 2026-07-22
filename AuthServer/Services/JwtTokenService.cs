using System.IdentityModel.Tokens.Jwt;
using System.Security.Claims;
using System.Text;
using AuthServer.Models;
using Microsoft.IdentityModel.Tokens;

namespace AuthServer.Services;

public class JwtTokenService
{
    private readonly IConfiguration _configuration;

    public JwtTokenService(IConfiguration configuration)
    {
        _configuration = configuration;
    }

    public string GenerateToken(User user)
    {
        var issuer = _configuration["Jwt:Issuer"]
                     ?? throw new InvalidOperationException("Jwt:Issuer not configured");
        var audience = _configuration["Jwt:Audience"]
                       ?? throw new InvalidOperationException("Jwt:Audience not configured");
        var secretKey = _configuration["Jwt:SecretKey"]
                        ?? throw new InvalidOperationException("Jwt:SecretKey not configured");

        var claims = new[]
        {
            new Claim(ClaimTypes.NameIdentifier, user.Id),
            new Claim(ClaimTypes.Name, user.Username),
            new Claim(ClaimTypes.Role, user.Role),
        };

        var key = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(secretKey));
        var credentials = new SigningCredentials(key, SecurityAlgorithms.HmacSha256);

        var token = new JwtSecurityToken(
            issuer: issuer,
            audience: audience,
            claims: claims,
            expires: DateTime.UtcNow.AddHours(1),
            signingCredentials: credentials
        );

        return new JwtSecurityTokenHandler().WriteToken(token);
    }
}