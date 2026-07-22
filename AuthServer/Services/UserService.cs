using AuthServer.Dtos;
using AuthServer.Models;

namespace AuthServer.Services;

public class UserService
{
    private readonly List<User> _users = new();
    private readonly ILogger<UserService> _logger;

    public UserService(ILogger<UserService> logger)
    {
        _logger = logger;
    }

    public Result<User> Register(RegisterRequest request)
    {
        if (_users.Any(u => u.Username == request.Username))
            return Result<User>.Failure("USERNAME_EXISTS", "이미 존재하는 사용자명입니다.");

        var user = new User
        {
            Username = request.Username,
            PasswordHash = request.Password
        };

        _users.Add(user);
        _logger.LogInformation("새 사용자 등록: {Username}", user.Username);

        return Result<User>.Success(user);
    }

    public void AddUserDirectly(User user)
    {
        _users.Add(user);
        _logger.LogWarning("{Role} User {Username} Directly Added", user.Role, user.Username);
    }

    public Result<User> Login(LoginRequest request)
    {
        var user = _users.FirstOrDefault(u =>
            u.Username == request.Username &&
            u.PasswordHash == request.Password);

        if (user == null)
            return Result<User>.Failure("INVALID_CREDENTIALS", "잘못된 사용자명 또는 비밀번호입니다.");

        return Result<User>.Success(user);
    }

    public User? GetById(string userId)
    {
        return _users.FirstOrDefault(u => u.Id == userId);
    }

    public User? GetByUsername(string username)
    {
        return _users.FirstOrDefault(u => u.Username == username);
    }
}