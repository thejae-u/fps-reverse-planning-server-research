using AuthServer.Models;
using AuthServer.Services;

namespace AuthServer.Services;

public class DataSeeder
{
    private readonly UserService _userService;
    private readonly IConfiguration _config;
    private readonly ILogger<DataSeeder> _logger;

    public DataSeeder(UserService userService, IConfiguration config, ILogger<DataSeeder> logger)
    {
        _userService = userService;
        _config = config;
        _logger = logger;
    }

    public async Task SeedAsync()
    {
        var adminUsername = _config["SeedData:Admin:Username"] ?? "admin";
        var adminPassword = _config["SeedData:Admin:Password"] ?? "adminPassword1234!";

        await SeedUserIfNotExistsAsync(adminUsername, adminPassword, "Admin");
        
        var internalUsername = _config["SeedData:Internal:Username"] ?? "internal";
        var internalPassword = _config["SeedData:Internal:Password"] ?? "internalPassword1234!";
        
        await SeedUserIfNotExistsAsync(internalUsername, internalPassword, "Internal");
    }

    private async Task SeedUserIfNotExistsAsync(string username, string password, string role)
    {
        var existingUser = await _userService.GetByUsernameAsync(username);
        if (existingUser != null)
        {
            _logger.LogInformation("[DataSeeder]  {Role} 계정('{Username}')이 이미 존재합니다.", role, username);
            return;
        }

        var user = new User
        {
            Username = username,
            PasswordHash = password,
            Role = role
        };
        
        await _userService.AddUserDirectlyAsync(user);
    }
}
